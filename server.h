#pragma once

#include <stddef.h>

namespace rdss {

static constexpr size_t QD = 1024;
static constexpr size_t READ_SIZE = 1024 * 16;
static constexpr size_t READ_THRESHOLD = 1024 * 32;

// TODO: Make these flags.
static constexpr bool SQ_POLL{false};
static constexpr bool DRAIN_CQ{false};
static constexpr bool SQE_ASYNC = false;

}
