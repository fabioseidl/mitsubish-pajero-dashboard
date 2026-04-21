#pragma once

#include <stdint.h>

class ServerConnectionMonitor {
public:
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 2000;

    using StatusChangeCallback = void (*)(bool online);

    explicit ServerConnectionMonitor(uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);

    void onPayloadReceived(uint32_t now_ms);
    void tick(uint32_t now_ms);
    bool isOnline() const;
    void setStatusChangeCallback(StatusChangeCallback cb);

private:
    uint32_t             timeout_ms_;
    uint32_t             last_received_ms_;
    bool                 is_online_;
    bool                 ever_received_;
    StatusChangeCallback callback_;
};
