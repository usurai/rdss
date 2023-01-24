#pragma once

#include "liburing.h"

namespace rdss {

class Proactor {
public:
    explicit Proactor(io_uring* ring)
      : ring_(ring) {}

    void Run();

private:
    io_uring* ring_;
};

} // namespace rdss
