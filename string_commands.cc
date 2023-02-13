#include "string_commands.h"

namespace rdss {

Result SetFunction(TrackingMap& data, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() != 3) {
        result.Add("error");
        return result;
    }

    auto key_ptr = std::make_shared<TrackingString>(command_strings[1]);
    auto value_ptr = std::make_shared<TrackingString>(command_strings[2]);
    data.InsertOrAssign(std::move(key_ptr), std::move(value_ptr));
    result.Add("inserted");
    return result;
}

Result GetFunction(TrackingMap& data, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() != 2) {
        result.Add("error");
        return result;
    }
    auto entry = data.Find(command_strings[1]);
    if (entry == nullptr) {
        result.AddNull();
    } else {
        // TODO: Eliminate the conversion.
        result.Add(std::string(*(entry->value)));
    }
    return result;
}

} // namespace rdss
