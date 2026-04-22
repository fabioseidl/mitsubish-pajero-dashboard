#include "espnow_receiver.h"
#include <string.h>

#ifndef UNIT_TEST
#include <esp_wifi.h>
#include <esp_now.h>
#endif

ESPNowReceiver* ESPNowReceiver::instance_ = nullptr;

bool ESPNowReceiver::begin(const uint8_t pmk[16]) {
    instance_ = this;
#ifndef UNIT_TEST
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_now_init();
    esp_now_set_pmk(pmk);
    esp_now_register_recv_cb([](const uint8_t* mac_addr, const uint8_t* data, int len) {
        ESPNowReceiver::onReceiveISR(mac_addr, data, len);
    });
#else
    (void)pmk;
#endif
    return true;
}

void ESPNowReceiver::setCallback(PayloadCallback cb) {
    instance_ = this;
    callback_ = cb;
}

void ESPNowReceiver::onReceiveISR(const uint8_t* mac, const uint8_t* data, int len) {
    (void)mac;
    if (!instance_) return;
    if (len != (int)sizeof(Payload)) return;

    Payload payload;
    memcpy(&payload, data, sizeof(Payload));

    if (payload.version != PAYLOAD_VERSION) return;
    if (!(payload.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    if (instance_->callback_) {
        instance_->callback_(payload);
    }
}
