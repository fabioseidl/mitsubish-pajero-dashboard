#pragma once
// Access point the tool brings up. Not a secret worth protecting: it is a
// short-lived AP in a parked car, and it carries no vehicle control, only
// read-only diagnostics plus an explicitly confirmed clear-codes call.
// Change these if you want; 8 characters is the WPA2 minimum.

static constexpr char     DIAG_AP_SSID[]    = "pajero-diag";
static constexpr char     DIAG_AP_PASSWORD[] = "pajero4m41";
static constexpr uint8_t  DIAG_AP_CHANNEL   = 6;

// Phrase the browser must send to confirm a Mode 04 clear.
static constexpr char     DIAG_CLEAR_TOKEN[] = "CLEAR";

static constexpr uint8_t  DIAG_MAX_CODES = 32;
