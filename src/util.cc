#include "util.h"

#include <glog/logging.h>
#include <sys/resource.h>

namespace rdss {

bool SetNofileLimit(uint32_t limit) {
    int ret;
    rlimit rlim;
    if ((ret = getrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(ERROR) << "getrlimit: " << strerror(errno);
        return false;
    }
    LOG(INFO) << "NOFILE rlimit: " << rlim.rlim_cur << ' ' << rlim.rlim_max;

    rlim.rlim_cur = limit;
    rlim.rlim_max = std::max<uint32_t>(rlim.rlim_max, limit);
    if ((ret = setrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(FATAL) << "setrlimit: " << strerror(errno);
    }

    if ((ret = getrlimit(RLIMIT_NOFILE, &rlim)) != 0) {
        LOG(ERROR) << "getrlimit: " << strerror(errno);
        return false;
    }

    LOG(INFO) << "NOFILE rlimit after modification: " << rlim.rlim_cur << ' ' << rlim.rlim_max;
    return (rlim.rlim_cur == limit && rlim.rlim_max == limit);
}

} // namespace rdss
