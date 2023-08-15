#include "client.h"

#include "base/buffer.h"
#include "client_manager.h"
#include "constants.h"
#include "resp/redis_parser.h"

#include <glog/logging.h>

namespace rdss {

namespace detail {

RedisParser::ParsingResult Parse(Buffer& buffer, bool parse_ongoing, MultiBulkParser* parser) {
    if (parse_ongoing) {
        return parser->Parse();
    }
    if (buffer.Source().at(0) == '*') {
        return parser->Parse();
    }
    return InlineParser::ParseInline(&buffer);
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
    Buffer query_buffer(kIOGenericBufferSize);
    // TODO: Make use of RedisParser::InProgress().
    bool parse_ongoing = false;
    // TODO: Lazily create parser.
    auto parser = std::make_unique<MultiBulkParser>(&query_buffer);
    Result query_result;

    while (true) {
        const auto data_start = query_buffer.EnsureAvailable(
          kIOGenericBufferSize, query_buffer.Capacity() < kIOGenericBufferSize);
        if (data_start != nullptr && parser->InProgress()) {
            parser->BufferUpdate(data_start, query_buffer.Start());
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

        VLOG(1) << "read length:" << bytes_read;

        auto [parse_result, command_strings] = detail::Parse(
          query_buffer, parse_ongoing, parser.get());
        switch (parse_result) {
        case RedisParser::State::kInit:
        case RedisParser::State::kParsing:
            parse_ongoing = true;
            continue;
        case RedisParser::State::kError:
            query_result.SetError(Error::kProtocol);
            break;
        case RedisParser::State::kDone:
            service_->Invoke(command_strings, query_result);
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
        if (query_result.NeedsScatter()) {
            bytes_written = co_await conn_->Writev(query_result.AsIovecs());
        } else {
            bytes_written = co_await conn_->Send(query_result.AsStringView());
        }
        if (bytes_written == 0) {
            break;
        }

        query_buffer.Reset();
        parse_ongoing = false;
        parser->Reset();
        query_result.Reset();
    }
    manager_->RemoveClient(conn_.get());
    conn_->Close();
    OnConnectionClose();
}

void Client::Disconnect() { cancel_token_.RequestCancel(); }

} // namespace rdss
