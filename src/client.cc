#include "client.h"

#include "base/buffer.h"
#include "client_manager.h"
#include "constants.h"
#include "data_structure_service.h"
#include "resp/replier.h"
#include "resp/resp_parser.h"
#include "runtime/ring_executor.h"

#include <glog/logging.h>

#include <tuple>

namespace rdss {

namespace detail {

// Parses data in 'buffer' inline or multi-bulk way according to 1.If mbulk parser is in
// progress 2.If the start of 'buffer' is '*'. If necessary, creates 'mbulk_parser'. Fills result
// into 'result', and updates 'result_size' to reflect the number of result.
ParserState Parse(
  Buffer& buffer,
  std::unique_ptr<MultiBulkParser>& mbulk_parser,
  StringViews& result,
  size_t& result_size) {
    if (mbulk_parser != nullptr && mbulk_parser->InProgress()) {
        auto res = mbulk_parser->Parse(result);
        if (res == ParserState::kDone) {
            result_size = mbulk_parser->GetResultSize();
        }
        return res;
    }
    if (buffer.Source().at(0) == '*') {
        if (mbulk_parser == nullptr) {
            mbulk_parser = std::make_unique<MultiBulkParser>(&buffer);
        }
        auto res = mbulk_parser->Parse(result);
        if (res == ParserState::kDone) {
            result_size = mbulk_parser->GetResultSize();
        }
        return res;
    }
    return ParseInline(&buffer, result, result_size);
}

} // namespace detail

Client::Client(Connection* conn, ClientManager* manager, DataStructureService* service)
  : conn_(std::unique_ptr<Connection>(conn))
  , manager_(manager)
  , service_(service) {}

Task<void> Client::Echo(RingExecutor* from) {
    if (from != conn_->GetExecutor()) {
        co_await Transfer(from, conn_->GetExecutor());
    }

    Buffer buffer(1024);
    std::error_code error;
    size_t bytes_read, bytes_written;
    while (true) {
        std::tie(error, bytes_read) = co_await conn_->Recv(buffer.Sink());
        if (error) {
            LOG(ERROR) << "recv: " << error.message();
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        buffer.Produce(bytes_read);

        std::tie(error, bytes_written) = co_await conn_->Send(buffer.Source());
        if (error) {
            LOG(ERROR) << "send: " << error.message();
            break;
        }
        if (bytes_written == 0) {
            break;
        }
        buffer.Consume(bytes_written);
        buffer.Reset();
    }
    Close();
}

Task<void> Client::Process(RingExecutor* from, RingExecutor* dss_executor) {
    if (from != conn_->GetExecutor()) {
        co_await Transfer(from, conn_->GetExecutor());
    }

    // Might expand before each call to recv() to ensure at least 'kIOGenericBufferSize'(16KB)
    // available. Should be reset after each round of serving to reclaim buffer space.
    Buffer query_buffer(kIOGenericBufferSize);
    // Lazily created multi-bulk parser. If it's in error/done state, it will automatically reset
    // upon new call to Parse().
    std::unique_ptr<MultiBulkParser> mbulk_parser{nullptr};
    // View of parsed arguments over 'query_buffer'. We don't clear it after round of serving to
    // avoid memory gets reclaim / allocate over the turns of serving.
    // TODO: Clear it if memory gets tight.
    StringViews arguments;
    // Ditto, lazily release of memory.
    Result query_result;
    Buffer output_buffer(kOutputBufferSize);
    // For output string/string array, that is, when reply is like "$6\r\nFOOBAR\r\n", there are 3
    // iovecs, first being view over "$6\r\n" in 'output_buffer', second being view over value
    // string shared_ptr in 'Result', the last being "\r\n" that references the same CRLF in the
    // first iovec.
    std::vector<iovec> iovecs;

    std::error_code error;
    size_t bytes_read, bytes_written;
    // size_t bytes_written;
    while (true) {
        // If 'query_buffer' gets expanded, it's current data is moved to new memory location. If
        // parser holds partial result view over the original memory of 'query_buffer', it should be
        // updated.
        const auto data_start = query_buffer.EnsureAvailable(
          kIOGenericBufferSize, query_buffer.Capacity() < kIOGenericBufferSize);
        if (data_start != nullptr && mbulk_parser != nullptr && mbulk_parser->InProgress()) {
            mbulk_parser->BufferUpdate(data_start, query_buffer.Start(), arguments);
        }

        // Normal recv
        // TODO: Optionally enable cancellable recv
        std::tie(error, bytes_read) = co_await conn_->Recv(query_buffer.Sink());
        if (error) {
            LOG(ERROR) << "recv: " << error.message();
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        query_buffer.Produce(bytes_read);

        size_t num_strings;
        const auto parse_result = detail::Parse(query_buffer, mbulk_parser, arguments, num_strings);
        switch (parse_result) {
        case ParserState::kInit:
        case ParserState::kParsing:
            continue;
        case ParserState::kError:
            query_result.SetError(Error::kProtocol);
            break;
        case ParserState::kDone:
            assert(num_strings != 0);
            co_await Transfer(conn_->GetExecutor(), dss_executor);
            service_->Invoke(std::span<StringView>(arguments.begin(), num_strings), query_result);
            co_await Transfer(dss_executor, conn_->GetExecutor());
            break;
        }

        // TODO: 1.Cancellable send. 2.Handle short write.
        // TODO: it's gather
        if (NeedsScatter(query_result)) {
            ResultToIovecs(query_result, output_buffer, iovecs);
            std::tie(error, bytes_written) = co_await conn_->Writev(iovecs);
        } else {
            std::tie(error, bytes_written) = co_await conn_->Send(
              ResultToStringView(query_result, output_buffer));
        }
        if (bytes_written == 0) {
            break;
        }

        query_buffer.Reset();
        query_result.Reset();
        output_buffer.Reset();
        iovecs.clear();
    }
    manager_->RemoveClient(conn_.get());
    conn_->Close();
    OnConnectionClose();
}

void Client::Close() {
    manager_->RemoveClient(conn_.get());
    conn_->Close();
    OnConnectionClose();
}

void Client::Disconnect() { cancel_token_.RequestCancel(); }

} // namespace rdss
