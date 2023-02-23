#include "client.h"

#include "buffer.h"
#include "client_manager.h"
#include "redis_parser.h"
#include "replier.h"

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
        LOG(INFO) << "read " << bytes_read;
        if (bytes_read == 0) {
            break;
        }
        buffer.Produce(bytes_read);
        auto bytes_written = co_await conn_->Send(std::string(buffer.Source()));
        LOG(INFO) << "written " << bytes_written;
        if (bytes_written == 0) {
            break;
        }
        buffer.Consume(bytes_written);
    }
    OnConnectionClose();
}

Task<void> Client::Process() {
    Buffer query_buffer(1024);
    // TODO: Make use of RedisParser::InProgress().
    bool parse_ongoing = false;
    // TODO: Lazily create parser.
    auto parser = std::make_unique<MultiBulkParser>(&query_buffer);
    Result query_result;
    while (true) {
        const auto [cancelled, bytes_read] = co_await conn_->CancellableRecv(
          query_buffer.Sink(), &cancel_token_);
        LOG(INFO) << "CancellableRecv returns: {" << cancelled << ", " << bytes_read << "}.";
        if (cancelled || bytes_read == 0) {
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
            query_result.Add("parse error");
            break;
        case RedisParser::State::kDone:
            query_result = service_->Invoke(command_strings);
            break;
        }

        // TODO: Handle short write.
        const auto [send_cancelled, bytes_written] = co_await conn_->CancellableSend(
          Replier::BuildReply(std::move(query_result)), &cancel_token_);
        if (send_cancelled || bytes_written == 0) {
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
