#pragma once

#include "resp/result.h"

#include <functional>
#include <memory>
#include <span>

namespace rdss {

class DataStructureService;

class Command {
public:
    using CommandString = std::string_view;
    using CommandStrings = std::span<CommandString>;
    using HandlerType = std::function<Result(DataStructureService&, CommandStrings)>;

    Command(std::string name)
      : name_(std::move(name)) {}

    Result operator()(DataStructureService& service, CommandStrings command_strings) {
        return handler_(service, std::move(command_strings));
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

using Args = Command::CommandStrings;

} // namespace rdss
