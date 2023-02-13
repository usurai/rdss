#include "data_structure_service.h"

#include <glog/logging.h>

namespace rdss {

Result DataStructureService::Invoke(Command::CommandStrings command_strings) {
    VLOG(1) << "received " << command_strings.size() << " commands:";
    for (const auto& arg : command_strings) {
        VLOG(1) << arg;
    }

    auto command = commands_.find(command_strings[0]);
    if (command == commands_.end()) {
        Result result;
        // TODO: this should be error
        result.Add("command not found");
        return result;
    }

    auto result = command->second(*data_, std::move(command_strings));
    return result;
}

} // namespace rdss
