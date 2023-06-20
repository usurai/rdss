#pragma once

#include <functional>
#include <optional>

namespace rdss {

class CancellationToken {
public:
    void RequestCancel() {
        cancel_requested_ = true;
        // TODO: operation captured by callback might have been released.
        if (cancellation_callback_.has_value()) {
            cancellation_callback_.value()();
        }
    }

    bool CancelRequested() const { return cancel_requested_; }

    void RegisterCancellationCallback(std::function<void()> callback) {
        cancellation_callback_.emplace(std::move(callback));
    }

    void DeregisterCancellationCallback() {
        cancellation_callback_.reset();
    }

private:
    bool cancel_requested_ = false;
    std::optional<std::function<void()>> cancellation_callback_;
};

} // namespace rdss
