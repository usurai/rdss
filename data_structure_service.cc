#include "data_structure_service.h"

#include <glog/logging.h>

namespace rdss {

Result DataStructureService::Invoke(CommandStrings command_strings) {
    LOG(INFO) << "received " << command_strings.size() << " commands:";
    for (const auto& arg : command_strings) {
        LOG(INFO) << arg;
    }

    Result result;
    result.Add("ok");
    return result;
}

} // namespace rdss
