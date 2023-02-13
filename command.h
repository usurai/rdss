#pragma once

#include "result.h"
#include "tracking_hash_table.h"

#include <functional>
#include <memory>
#include <span>

namespace rdss {

class Command {
public:
    using CommandStrings = std::span<std::string_view>;
    using HandlerType = std::function<Result(TrackingMap&, CommandStrings)>;

    Command(std::string name)
      : name_(std::move(name)) {}

    Result operator()(TrackingMap& data, CommandStrings command_strings) {
        return handler_(data, std::move(command_strings));
    }

    Command& SetHandler(HandlerType handler) {
        handler_ = std::move(handler);
        return *this;
    }

    Command& SetIsWriteCommand() {
        is_write_command_ = true;
        return *this;
    }

    bool IsWriteCommand() const { return is_write_command_; }

private:
    const std::string name_;
    bool is_write_command_ = false;
    HandlerType handler_;
};

} // namespace rdss
