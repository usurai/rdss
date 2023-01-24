#include "async_operation.h"

namespace rdss {

void AcceptOperation::Initiate(std::coroutine_handle<> continuation) {
    SetContinuation(std::move(continuation));
    processor_->Execute(this);
}
} // namespace rdss
