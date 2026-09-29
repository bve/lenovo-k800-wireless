#pragma once
#include "HostProfiles.hpp"
#include "ReportQueue.hpp"

namespace bridge {
// Transport provides live(), release(), bootMode(), keyboard() and consumer().
// No queued or held input crosses a host boundary, even under backpressure.
class HidRouter {
public:
    template<class Transport> bool route(HidEndpoint target, Transport& transport) {
        if (target == endpoint_ && !switching_) return false;
        switching_ = true;
        queue_.clear();
        blocked_ = true;
        if (transport.live(endpoint_) && !transport.release(endpoint_)) return false;
        endpoint_ = target;
        switching_ = false;
        desired_ = sent_ = Reports{};
        keyboardSent_ = consumerSent_ = false;
        queue_.reset(desired_);
        return true;
    }
    void update(const Reports& report) {
        if (blocked_) {
            if (equal(report, Reports{})) blocked_ = false;
            else return;
        }
        if (!endpoint_.valid() || equal(report, desired_)) return;
        desired_ = report; queue_.push(report);
    }
    void reset() {
        desired_ = Reports{}; queue_.reset(desired_);
        keyboardSent_ = consumerSent_ = false; blocked_ = true;
    }
    bool idle() const { return queue_.empty() && !blocked_ && !switching_; }
    bool blocked() const { return blocked_; }
    HidEndpoint endpoint() const { return endpoint_; }
    template<class Transport> void poll(uint32_t now, Transport& transport) {
        if (switching_) return;
        if (!transport.live(endpoint_)) { queue_.clear(); return; }
        const bool boot = transport.bootMode(endpoint_);
        if (boot != previousBoot_ || (queue_.empty() && now - refreshed_ >= 10000)) {
            keyboardSent_ = consumerSent_ = false; previousBoot_ = boot; refreshed_ = now;
            queue_.reset(desired_);
        }
        if (queue_.empty()) return;
        const auto& report = queue_.front();
        if ((!keyboardSent_ || memcmp(report.keyboard, sent_.keyboard, 8)) &&
            transport.keyboard(endpoint_, report.keyboard)) {
            memcpy(sent_.keyboard, report.keyboard, 8); keyboardSent_ = true;
        }
        if ((!consumerSent_ || report.consumer != sent_.consumer) &&
            transport.consumer(endpoint_, report.consumer)) {
            sent_.consumer = report.consumer; consumerSent_ = true;
        }
        if (keyboardSent_ && consumerSent_ && equal(report, sent_)) queue_.pop();
    }
private:
    HidEndpoint endpoint_;
    ReportQueue queue_;
    Reports desired_, sent_;
    uint32_t refreshed_ = 0;
    bool keyboardSent_ = false, consumerSent_ = false, previousBoot_ = false, blocked_ = true, switching_ = false;
};
}
