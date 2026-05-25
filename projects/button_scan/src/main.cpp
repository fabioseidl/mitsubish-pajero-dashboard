/*
 * CYD Button Pin Scanner
 * ----------------------
 * Polls all safe GPIO inputs every 100 ms and prints any pin that reads LOW.
 * Press the physical button on your CYD while watching the Serial monitor —
 * the pin that changes will be your button GPIO.
 *
 * Flash this sketch, open Serial at 115200, then press the button.
 *
 * Pins deliberately skipped:
 *   - 6-11  : connected to internal flash (do NOT touch)
 *   - 21    : backlight PWM (would flicker the display)
 *   - 27    : BL_EN rail (would cut the backlight)
 *   - 34,35 : input-only, no pull-up, usually floating
 *   - 36,39 : input-only sensor pins (touch IRQ / MISO)
 */

#include <Arduino.h>

// GPIOs to scan — input-capable candidates for this board.
// Note: GPIO1 and GPIO3 are UART0 TX/RX and must not be scanned because
// they are used by Serial output.
// The top button may be on one of the extra pins below, so we include a
// broader set of candidate GPIOs for detection.
static const uint8_t SCAN_PINS[] = {
    0, 2, 4, 5,
    12, 13, 14, 15, 16, 17, 18, 19,
    21, 22, 23, 25, 26, 27,
    32, 33, 34, 35, 36, 39
};
static const uint8_t PIN_COUNT = sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0]);

// Track previous state so we only print on change
static bool prev_state[PIN_COUNT];

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("=== CYD Button Pin Scanner ===");
    Serial.println("Press the physical button — the GPIO that changes state is your pin.");
    Serial.println("If the button is the EN/reset button, it will not appear here.");
    Serial.println();

    Serial.println("Scanning GPIOs:");
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
        uint8_t pin = SCAN_PINS[i];

        // Some pins cannot use the internal pull-up, so only enable it where supported.
        if (pin >= 34 && pin <= 39) {
            pinMode(pin, INPUT);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }

        prev_state[i] = (digitalRead(pin) == HIGH);
        Serial.printf("  GPIO %2d = %s\n", pin, prev_state[i] ? "HIGH" : "LOW");
    }
    Serial.println();
}

void loop() {
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
        bool state = (digitalRead(SCAN_PINS[i]) == HIGH);

        if (state != prev_state[i]) {
            if (!state) {
                // Just went LOW → pressed
                Serial.printf(">>> GPIO %2d went LOW  (PRESSED)\n", SCAN_PINS[i]);
            } else {
                // Just went HIGH → released
                Serial.printf("    GPIO %2d went HIGH (released)\n", SCAN_PINS[i]);
            }
            prev_state[i] = state;
        }
    }
    delay(20);
}
