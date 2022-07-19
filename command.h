#pragma once

#include "dragonfly/resp_expr.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rdss {

struct Result {
    std::vector<std::string> data;
    std::vector<bool> is_null;

    void Add(std::string s) {
        data.push_back(std::move(s));
        is_null.push_back(false);
    }

    void AddNull() {
        data.push_back({});
        is_null.push_back(true);
    }

    size_t Size() const { return data.size(); }
};

class Command {
public:
    Command(std::string name)
      : name_(std::move(name)) {}

    Command& SetHandler(std::function<Result(facade::RespExpr::Vec& vec)> handler) {
        handler_ = std::move(handler);
        return *this;
    }

    Result operator()(facade::RespExpr::Vec& vec) { return handler_(vec); }

private:
    const std::string name_;
    std::function<Result(facade::RespExpr::Vec& vec)> handler_;
};

} // namespace rdss
