#include "espnow_broadcaster.h"
#include <string.h>

#ifndef UNIT_TEST
#include <esp_wifi.h>
#include <esp_now.h>
#endif

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

ESPNowBroadcaster* g_broadcaster_instance = nullptr;

bool ESPNowBroadcaster::begin(const uint8_t pmk[16]) {
    initialized_      = false;
    last_send_status_ = ESP_NOW_SEND_SUCCESS;

#ifndef UNIT_TEST
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    if (esp_now_set_pmk(pmk) != ESP_OK) return false;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    last_add_peer_err_ = esp_now_add_peer(&peer);

    g_broadcaster_instance = this;
#ifdef ARDUINO
    esp_now_register_send_cb([](const uint8_t* /*mac_addr*/, esp_now_send_status_t status) {
        ESPNowBroadcaster::onSendComplete(nullptr, status);
    });
#else
    esp_now_register_send_cb([](const wifi_tx_info_t* /*info*/, esp_now_send_status_t status) {
        ESPNowBroadcaster::onSendComplete(nullptr, status);
    });
#endif
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
