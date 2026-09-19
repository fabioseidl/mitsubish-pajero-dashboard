#include "espnow_broadcaster.h"
#include <string.h>

#ifndef UNIT_TEST
#include <esp_idf_version.h>   // ESP_IDF_VERSION / ESP_IDF_VERSION_VAL
#include <esp_wifi.h>
#include <esp_now.h>
#ifdef ARDUINO
#include <WiFi.h>
#endif
#endif

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#ifndef UNIT_TEST
// The send callback's FIRST parameter type differs between SDKs: older ones pass
// `const uint8_t* mac_addr`, newer ones `const wifi_tx_info_t*`. Rather than guess,
// deduce it from the SDK's OWN typedef, so this adapts to whatever is installed.
//
// This replaced an `#ifdef ARDUINO` split, which conflated framework with SDK
// version and broke the espidf-framework emulator. A version threshold is no better:
// IDF 5.4.2 still ships the uint8_t form and defines no wifi_tx_info_t at all. A
// generic (`auto*`) lambda would also work but needs C++14, and client_simple_hud
// still builds as C++11 — this trait is C++11-clean.
template <typename F> struct EspNowSendCbTraits;
template <typename A> struct EspNowSendCbTraits<void (*)(A, esp_now_send_status_t)> {
    using Arg0 = A;
};
using EspNowSendCbArg0 = EspNowSendCbTraits<esp_now_send_cb_t>::Arg0;
#endif

ESPNowBroadcaster* g_broadcaster_instance = nullptr;

bool ESPNowBroadcaster::begin(const uint8_t pmk[16]) {
    initialized_      = false;
    last_send_status_ = ESP_NOW_SEND_SUCCESS;

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
    if (esp_now_init() != ESP_OK) return false;
    if (esp_now_set_pmk(pmk) != ESP_OK) return false;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    // encrypt MUST be false: ESP-NOW only encrypts unicast, and esp_now_add_peer()
    // rejects a broadcast peer with encryption on. So the esp_now_set_pmk() call
    // above does not protect this traffic — the Payload is sent in the clear and
    // is unauthenticated. Clients that care pin the sender MAC instead; see
    // ESPNowReceiver::setExpectedSender() and security_config.h.example.
    peer.encrypt = false;
    last_add_peer_err_ = esp_now_add_peer(&peer);

    g_broadcaster_instance = this;
    // First argument is unused; its type comes from EspNowSendCbArg0 above so this
    // compiles against either SDK signature. Captureless, so it still converts to
    // esp_now_send_cb_t.
    esp_now_register_send_cb([](EspNowSendCbArg0 /*mac_or_tx_info*/, esp_now_send_status_t status) {
        ESPNowBroadcaster::onSendComplete(nullptr, status);
    });
#else
    (void)pmk;
#endif

    initialized_ = true;
    return true;
}

bool ESPNowBroadcaster::send(const Payload& payload) {
    if (!initialized_) return false;
#ifndef UNIT_TEST
    esp_err_t err = esp_now_send(BROADCAST_MAC,
                                  (const uint8_t*)&payload,
                                  sizeof(Payload));
    last_send_err_ = err;
    return err == ESP_OK;
#else
    (void)payload;
    return true;
#endif
}

esp_now_send_status_t ESPNowBroadcaster::lastSendStatus() const {
    return last_send_status_;
}

void ESPNowBroadcaster::onSendComplete(const uint8_t* /*mac*/,
                                        esp_now_send_status_t status) {
    if (g_broadcaster_instance) {
        g_broadcaster_instance->last_send_status_ = status;
    }
}
