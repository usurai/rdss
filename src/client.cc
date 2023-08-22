#include "client.h"

#include "base/buffer.h"
#include "client_manager.h"
#include "constants.h"
#include "resp/replier.h"
#include "resp/resp_parser.h"

#include <glog/logging.h>

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

Task<void> Client::Echo() {
    Buffer buffer(1024);
    while (true) {
        const auto bytes_read = co_await conn_->Recv(buffer.Sink());
        if (bytes_read == 0) {
            break;
        }
        buffer.Produce(bytes_read);
        auto bytes_written = co_await conn_->Send(buffer.Source());
        if (bytes_written == 0) {
            break;
        }
        buffer.Consume(bytes_written);
        buffer.Reset();
    }
    manager_->RemoveClient(conn_.get());
    conn_->Close();
    OnConnectionClose();
}

Task<void> Client::Process() {
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

    while (true) {
        // If 'query_buffer' gets expanded, it's current data is moved to new memory location. If
        // parser holds partial result view over the original memory of 'query_buffer', it should be
        // updated.
        const auto data_start = query_buffer.EnsureAvailable(
          kIOGenericBufferSize, query_buffer.Capacity() < kIOGenericBufferSize);
        if (data_start != nullptr && mbulk_parser != nullptr && mbulk_parser->InProgress()) {
            mbulk_parser->BufferUpdate(data_start, query_buffer.Start(), arguments);
        }

        // Ideally:
        // auto some_result = co_await conn_->Recv(SOME_PROCESSOR,  buffer, cancel_token_,);
        // Underlying, recv sqe is submitted to ring of SOME_PROCESSOR

        // Cancellable recv
        // const auto [cancelled, bytes_read] = co_await conn_->CancellableRecv(
        //   query_buffer.Sink(), &cancel_token_);
        // VLOG(1) << "CancellableRecv returns: {" << cancelled << ", " << bytes_read << "}.";
        // if (cancelled || bytes_read == 0) {
        //     break;
        // }

        // Normal recv
        const auto bytes_read = co_await conn_->Recv(query_buffer.Sink());
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
            service_->Invoke(std::span<StringView>(arguments.begin(), num_strings), query_result);
            break;
        }

        // TODO: Handle short write.
        // Cancellable send
        // const auto [send_cancelled, bytes_written] = co_await conn_->CancellableSend(
        //   Replier::BuildReply(std::move(query_result)), &cancel_token_);
        // if (send_cancelled || bytes_written == 0) {
        //     break;
        // }

        size_t bytes_written{0};
        // TODO: it's gather
        if (NeedsScatter(query_result)) {
            ResultToIovecs(query_result, output_buffer, iovecs);
            bytes_written = co_await conn_->Writev(iovecs);
        } else {
            bytes_written = co_await conn_->Send(ResultToStringView(query_result, output_buffer));
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

void Client::Disconnect() { cancel_token_.RequestCancel(); }

} // namespace rdss
