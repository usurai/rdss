#include "client.h"

#include "base/buffer.h"
#include "client_manager.h"
#include "constants.h"
#include "resp/replier.h"
#include "runtime/util.h"
#include "service/data_structure_service.h"

#include <glog/logging.h>

#include <tuple>

namespace rdss {

namespace detail {

// Parses data in 'buffer' inline or multi-bulk way according to
// 1.If mbulk parser is in progress.
// 2.If the start of 'buffer' is '*'.
// If necessary, creates 'mbulk_parser_'. Fills result into 'result', and updates 'result_size' to
// reflect the number of result.
ParserState Parse(
  Buffer& buffer,
  std::unique_ptr<MultiBulkParser>& mbulk_parser_,
  StringViews& result,
  size_t& result_size) {
    if (mbulk_parser_ != nullptr && mbulk_parser_->InProgress()) {
        auto res = mbulk_parser_->Parse(result);
        if (res == ParserState::kDone) {
            result_size = mbulk_parser_->GetResultSize();
        }
        return res;
    }
    if (buffer.Source().at(0) == '*') {
        if (mbulk_parser_ == nullptr) {
            mbulk_parser_ = std::make_unique<MultiBulkParser>(&buffer);
        }
        auto res = mbulk_parser_->Parse(result);
        if (res == ParserState::kDone) {
            result_size = mbulk_parser_->GetResultSize();
        }
        return res;
    }
    return ParseInline(&buffer, result, result_size);
}

} // namespace detail

Client::Client(Connection* conn, ClientManager* manager, DataStructureService* service)
  : conn_(std::unique_ptr<Connection>(conn))
  , manager_(manager)
  , service_(service)
  , query_buffer_(
      conn_->UseRingBuf() ? 0 /* Init with 0 will make buffer 'virtual_view' */
                          : kIOGenericBufferSize)
  , output_buffer_(kOutputBufferSize) {}

Task<void> Client::Process(RingExecutor* dss_executor) {
    while (true) {
        EnsureBuffer();
        auto [err, buffer_view] = co_await conn_->Recv(&query_buffer_);
        if (err) {
            VLOG(1) << "recv: " << err.message();
            break;
        }
        const auto bytes_read = query_buffer_.Source().size();
        if (bytes_read == 0) {
            break;
        }
        manager_->Stats().net_input_bytes.fetch_add(bytes_read, std::memory_order_relaxed);

        size_t num_strings;
        const auto parse_result = detail::Parse(
          query_buffer_, mbulk_parser_, arguments_, num_strings);
        switch (parse_result) {
        case ParserState::kInit:
        case ParserState::kParsing:
            continue;
        case ParserState::kError:
            query_result_.SetError(Error::kProtocol);
            break;
        case ParserState::kDone:
            assert(num_strings != 0);
            // TODO: Add new Transfer that doesn't need to specify src executor. Like resume_on.
            co_await Transfer(conn_->GetExecutor(), dss_executor);
            service_->Invoke(std::span<StringView>(arguments_.begin(), num_strings), query_result_);
            co_await Transfer(dss_executor, conn_->GetExecutor());
            break;
        }
        conn_->PutBufferView(std::move(buffer_view));

        std::error_code error;
        size_t bytes_written;
        if (NeedsGather(query_result_)) {
            ResultToIovecs(query_result_, output_buffer_, iovecs_);
            std::tie(error, bytes_written) = co_await conn_->Writev(iovecs_);
        } else {
            std::tie(error, bytes_written) = co_await conn_->Send(
              ResultToStringView(query_result_, output_buffer_));
        }
        if (error) {
            LOG(ERROR) << "writev or send:" << error.message();
            break;
        }
        if (bytes_written == 0) {
            break;
        }
        manager_->Stats().UpdateOutputBufferSize(output_buffer_.Capacity());
        manager_->Stats().net_output_bytes.fetch_add(bytes_written, std::memory_order_relaxed);
        ResetState();
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

void Client::EnsureBuffer() {
    if (query_buffer_.IsVirtual()) {
        return;
    }

    // If 'query_buffer_' gets expanded, it's current data is moved to new memory location.
    // If parser holds partial result view over the original memory of 'query_buffer_', it
    // should be updated.
    const auto data_start = query_buffer_.EnsureAvailable(
      kIOGenericBufferSize, query_buffer_.Capacity() < kIOGenericBufferSize);
    if (data_start != nullptr && mbulk_parser_ != nullptr && mbulk_parser_->InProgress()) {
        mbulk_parser_->BufferUpdate(data_start, query_buffer_.Start(), arguments_);
    }
    manager_->Stats().UpdateInputBufferSize(query_buffer_.Capacity());
}

void Client::ResetState() {
    query_buffer_.Reset();
    output_buffer_.Reset();
    query_result_.Reset();
    iovecs_.clear();
}

} // namespace rdss
