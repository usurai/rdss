#pragma once

#include "dragonfly/resp_expr.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rdss {

struct Result {
    size_t count{0};
    std::vector<std::string> results;
};

class Command {
public:
    Command(std::string name)
      : name_(std::move(name)) {}

    Command& SetHandler(std::function<Result(facade::RespExpr::Vec& vec)> handler) {
        handler_ = std::move(handler);
        return *this;
    }

    Result operator()(facade::RespExpr::Vec& vec) {
        return handler_(vec);
    }

private:
    const std::string name_;
    std::function<Result(facade::RespExpr::Vec& vec)> handler_;
};

} // namespace rdss
