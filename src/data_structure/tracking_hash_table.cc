#include "data_structure/tracking_hash_table.h"

#include "glog/logging.h"

namespace rdss {

MTSPtr CreateMTSPtr(std::string_view sv) {
    VLOG(1) << "Creating shared string pointer.";
    return std::allocate_shared<MTS>(Mallocator<MTS>(), sv);
}

} // namespace rdss
