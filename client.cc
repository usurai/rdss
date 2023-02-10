#include "client.h"

#include "buffer.h"
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
        parser->Reset();
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
    bool parse_ongoing = false;
    auto parser = std::make_unique<MultiBulkParser>(&query_buffer);

    while (true) {
        size_t bytes_read = co_await conn_->Recv(query_buffer.Sink());
        if (bytes_read == 0) {
            OnConnectionClose();
            co_return;
        }
        query_buffer.Produce(bytes_read);

        auto [parse_result, command_strings] = detail::Parse(
          query_buffer, parse_ongoing, parser.get());
        switch (parse_result) {
        case RedisParser::State::kInit:
        case RedisParser::State::kParsing:
            parse_ongoing = true;
            continue;
        case RedisParser::State::kError:
            // TODO: co_await reply error
            LOG(INFO) << "parse error";
            query_buffer.Reset();
            parser->Reset();
            parse_ongoing = false;
            continue;
        case RedisParser::State::kDone:
            parse_ongoing = false;
            break;
        }

        auto result = service_->Invoke(command_strings);

        query_buffer.Reset();
        parser->Reset();

        [[maybe_unused]] const auto bytes_written = co_await conn_->Send(
          Replier::BuildReply(std::move(result)));
    }
}

} // namespace rdss
