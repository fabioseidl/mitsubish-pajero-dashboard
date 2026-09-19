#include "espnow_receiver.h"
#include <string.h>

#ifndef UNIT_TEST
#include <esp_idf_version.h>   // ESP_IDF_VERSION / ESP_IDF_VERSION_VAL
#include <esp_wifi.h>
#ifdef ARDUINO
#include <WiFi.h>
#include <Arduino.h>
#endif
#include <esp_now.h>
#endif

ESPNowReceiver* ESPNowReceiver::instance_ = nullptr;
uint32_t        ESPNowReceiver::raw_rx_count_ = 0;
uint32_t        ESPNowReceiver::rejected_sender_count_ = 0;
uint8_t         ESPNowReceiver::expected_mac_[6] = {0};
bool            ESPNowReceiver::filter_sender_ = false;

void ESPNowReceiver::setExpectedSender(const uint8_t mac[6]) {
    memcpy(expected_mac_, mac, 6);
    filter_sender_ = true;
}

void ESPNowReceiver::clearExpectedSender() {
    filter_sender_ = false;
}

bool ESPNowReceiver::begin(const uint8_t pmk[16]) {
    instance_ = this;
#ifdef ESPNOW_SERVER_MAC
    // Compile-time sender pinning from security_config.h.
    {
        static const uint8_t kServerMac[6] = ESPNOW_SERVER_MAC;
        setExpectedSender(kServerMac);
    }
#endif
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
    // The receive-callback signature is set by the IDF version, NOT by whether this
    // is an Arduino build: IDF 5.0 replaced the bare `const uint8_t* mac` first
    // argument with `const esp_now_recv_info*`. Arduino-ESP32 2.x ships IDF 4.4 and
    // 3.x ships IDF 5.x, and `platform = espressif32` is unpinned in the
    // sub-projects, so both have to compile here. Selecting on ARDUINO (as this did
    // originally) picks the 4.x signature on an Arduino 3.x build and fails to
    // convert the lambda.
    esp_err_t cb_err;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    cb_err = esp_now_register_recv_cb([](const esp_now_recv_info* recv_info, const uint8_t* data, int len) {
        ESPNowReceiver::onReceiveISR(recv_info->src_addr, data, len);
    });
#else
    cb_err = esp_now_register_recv_cb([](const uint8_t* mac_addr, const uint8_t* data, int len) {
        ESPNowReceiver::onReceiveISR(mac_addr, data, len);
    });
#endif
#ifdef ARDUINO
    Serial.printf("[ESPNOW] register_recv_cb=%d channel=%d\n", cb_err, WiFi.channel());
#else
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
    if (!instance_) return;

    // Sender check first: cheapest rejection, and the only thing standing between
    // a stranger's broadcast and the dashboard. See setExpectedSender().
    if (filter_sender_) {
        if (mac == nullptr || memcmp(mac, expected_mac_, 6) != 0) {
            ++rejected_sender_count_;
            return;
        }
    }

    if (len != (int)sizeof(Payload)) return;

    Payload payload;
    memcpy(&payload, data, sizeof(Payload));

    if (payload.version != PAYLOAD_VERSION) return;

    if (instance_->callback_) {
        instance_->callback_(payload);
    }
}
