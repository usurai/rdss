#pragma once

#include <chrono>

namespace rdss {

class Clock {
public:
    using TimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

    explicit Clock(bool is_system)
      : is_system_(is_system) {}

    TimePoint Now() const {
        if (is_system_) {
            return std::chrono::time_point_cast<TimePoint::duration>(TimePoint::clock::now());
        }
        return time_;
    }

    void SetTime(TimePoint time) { time_ = time; }

private:
    bool is_system_;
    TimePoint time_;
};

} // namespace rdss
