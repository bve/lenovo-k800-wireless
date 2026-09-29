#pragma once
#include "KeyMapper.hpp"

namespace bridge {
// Preserve taps while BLE notifications are temporarily backpressured.
class ReportQueue {
public:
    bool empty() const { return size_ == 0; }
    const Reports& front() const { return data_[head_]; }
    void clear() { head_ = size_ = 0; }
    void reset(const Reports& report) { clear(); push(report); }
    void pop() {
        if (size_) { head_ = (head_ + 1) % kCapacity; --size_; }
    }
    void push(const Reports& report) {
        if (size_ && equal(data_[(head_ + size_ - 1) % kCapacity], report)) return;
        if (size_ == kCapacity) {
            // Bounded overflow policy: release before restoring the latest state.
            clear();
            data_[size_++] = Reports{};
            if (equal(data_[0], report)) return;
        }
        data_[(head_ + size_) % kCapacity] = report;
        ++size_;
    }
private:
    static constexpr size_t kCapacity = 16;
    Reports data_[kCapacity] = {};
    size_t head_ = 0;
    size_t size_ = 0;
};
}
