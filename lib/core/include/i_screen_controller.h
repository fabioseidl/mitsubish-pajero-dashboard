#pragma once

#include "payload.h"

class IScreenController {
public:
    virtual ~IScreenController() = default;

    virtual bool begin() = 0;
    virtual void onPayloadReceived(const Payload& payload) = 0;
    virtual void onServerStatusChanged(bool online) = 0;
    virtual void tick() = 0;
};
