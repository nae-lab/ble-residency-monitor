#include "ble_lifecycle.h"

#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEScan.h>

bool bleShutdownForModeSwitch() {
    if (!BLEDevice::getInitialized()) {
        return true;
    }

    BLEDevice::stopAdvertising();

    BLEScan* scan = BLEDevice::getScan();
    if (scan != nullptr && scan->isScanning()) {
        scan->stop();
        delay(100);
    }

    // Never release controller memory; deinit(true) prevents re-init on ESP32.
    BLEDevice::deinit(false);
    delay(300);
    return true;
}

bool bleEnsureReady(const char* deviceName, uint8_t maxAttempts) {
    if (BLEDevice::getInitialized()) {
        return true;
    }

    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
        if (BLEDevice::init(deviceName)) {
            return true;
        }

        Serial.printf("[ble] init failed (attempt %u/%u)\n",
                      static_cast<unsigned>(attempt + 1),
                      static_cast<unsigned>(maxAttempts));
        delay(400);
    }

    return false;
}
