#include "result.h"

namespace rdss {

void Result::SetError(Error err) {
    type = Type::kError;
    error = err;
}

void Result::SetString(MTSPtr str) {
    type = Type::kString;
    string_ptr = std::move(str);
}

void Result::SetInt(int64_t val) {
    type = Type::kInt;
    int_value = val;
}

void Result::AddString(MTSPtr str) {
    type = Type::kStrings;
    strings.push_back(std::move(str));
}

void Result::Reset() {
    type = Type::kOk;
    string_ptr.reset();
    strings.clear();
}

} // namespace rdss
