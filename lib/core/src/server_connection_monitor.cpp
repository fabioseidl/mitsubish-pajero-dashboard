#include "server_connection_monitor.h"

ServerConnectionMonitor::ServerConnectionMonitor(uint32_t timeout_ms)
    : timeout_ms_(timeout_ms),
      last_received_ms_(0),
      is_online_(false),
      ever_received_(false),
      callback_(nullptr) {}

void ServerConnectionMonitor::onPayloadReceived(uint32_t now_ms) {
    last_received_ms_ = now_ms;
    ever_received_    = true;
}

void ServerConnectionMonitor::tick(uint32_t now_ms) {
    if (!ever_received_) {
        return;
    }

    bool should_be_online = (now_ms - last_received_ms_) < timeout_ms_;

    if (should_be_online && !is_online_) {
        is_online_ = true;
        if (callback_) callback_(true);
    } else if (!should_be_online && is_online_) {
        is_online_ = false;
        if (callback_) callback_(false);
    }
}

bool ServerConnectionMonitor::isOnline() const {
    return is_online_;
}

void ServerConnectionMonitor::setStatusChangeCallback(StatusChangeCallback cb) {
    callback_ = cb;
}
