#include "espnow_receiver.h"
#include <string.h>

#ifndef UNIT_TEST
#include <esp_wifi.h>
#ifdef ARDUINO
#include <WiFi.h>
#include <Arduino.h>
#endif
#include <esp_now.h>
#endif

ESPNowReceiver* ESPNowReceiver::instance_ = nullptr;
uint32_t        ESPNowReceiver::raw_rx_count_ = 0;

bool ESPNowReceiver::begin(const uint8_t pmk[16]) {
    instance_ = this;
#ifndef UNIT_TEST
#ifdef ARDUINO
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
#else
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;
#endif
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_now_init() != ESP_OK) {
#ifdef ARDUINO
        Serial.println("[ESPNOW] esp_now_init failed");
#endif
        return false;
    }
    if (esp_now_set_pmk(pmk) != ESP_OK) {
#ifdef ARDUINO
        Serial.println("[ESPNOW] esp_now_set_pmk failed");
#endif
        return false;
    }
    esp_err_t cb_err;
#ifdef ARDUINO
    cb_err = esp_now_register_recv_cb([](const uint8_t* mac_addr, const uint8_t* data, int len) {
        ESPNowReceiver::onReceiveISR(mac_addr, data, len);
    });
    Serial.printf("[ESPNOW] register_recv_cb=%d channel=%d\n", cb_err, WiFi.channel());
#else
    cb_err = esp_now_register_recv_cb([](const esp_now_recv_info* recv_info, const uint8_t* data, int len) {
        ESPNowReceiver::onReceiveISR(recv_info->src_addr, data, len);
    });
    (void)cb_err;
#endif
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
    ++raw_rx_count_;
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
