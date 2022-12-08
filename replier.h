#pragma once

#include "command.h"

#include <sstream>

namespace rdss {

class Replier {
public:
    static std::string BuildReply(Result result) {
        std::stringstream stream;

        if (result.type == Result::Type::kString) {
            stream << '*' << result.Size() << "\r\n";
            for (size_t i = 0; i < result.Size(); ++i) {
                if (result.is_null[i]) {
                    stream << "$-1\r\n";
                } else {
                    stream << '$' << result.data[i].size() << "\r\n";
                    stream << std::move(result.data[i]) << "\r\n";
                }
            }
        } else {
            if (result.Size() > 1) {
                stream << '*' << result.Size() << "\r\n";
                for (size_t i = 0; i < result.Size(); ++i) {
                    stream << ':' << result.ints[i] << "\r\n";
                }
            } else {
                stream << ':' << result.ints[0] << "\r\n";
            }
        }
        return stream.str();
    }
};

} // namespace rdss
