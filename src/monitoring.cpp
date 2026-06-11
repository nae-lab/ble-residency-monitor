#include "monitoring.h"
#include "ble_lifecycle.h"

#include <cstring>

#include <M5Unified.h>

#include <BLEDevice.h>
#include <BLEScan.h>
#include <esp_gap_ble_api.h>

#include "rpa_resolve.h"

namespace {
constexpr uint32_t kPresenceTimeoutMs = 45000;
constexpr uint32_t kUiRefreshMs = 1000;
constexpr float kRssiEmaAlpha = 0.3f;

EnrolleeStore* g_store = nullptr;
uint32_t g_scanCount = 0;
uint32_t g_resolvableCount = 0;
uint32_t g_matchCount = 0;
bool g_loggedRpaDiagnostic = false;

void formatAddr(const uint8_t addr[6], char* out, size_t outLen) {
    snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void copyAddr(const uint8_t* src, uint8_t addr[6]) {
    memcpy(addr, src, 6);
}

bool addrEqualsIdentity(const uint8_t addr[6], const uint8_t identityAddr[6]) {
    return memcmp(addr, identityAddr, 6) == 0;
}

int findMatchingEnrollee(const uint8_t addr[6]) {
    if (g_store == nullptr) {
        return -1;
    }

    for (size_t i = 0; i < g_store->count(); ++i) {
        const Enrollee* e = g_store->get(i);
        if (e == nullptr) {
            continue;
        }
        if (addrEqualsIdentity(addr, e->identityAddr) || rpaResolve(addr, e->irk)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

class MonitorScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        uint8_t addr[6] = {};
        copyAddr(advertisedDevice.getAddress().getNative(), addr);

        const uint8_t addrType = advertisedDevice.getAddressType();
        const int rssi = advertisedDevice.getRSSI();
        ++g_scanCount;

        char addrStr[18];
        formatAddr(addr, addrStr, sizeof(addrStr));
        Serial.printf("[scan] #%lu %s type=%u rssi=%d\n",
                      static_cast<unsigned long>(g_scanCount), addrStr, addrType, rssi);

        if (rpaIsResolvable(addr)) {
            ++g_resolvableCount;
            Serial.printf("[scan] resolvable RPA %s\n", addrStr);

            if (!g_loggedRpaDiagnostic && g_store != nullptr && rssi >= -55) {
                for (size_t i = 0; i < g_store->count(); ++i) {
                    const Enrollee* e = g_store->get(i);
                    if (e != nullptr) {
                        rpaLogResolveDiagnostic(addr, e->irk, e->label);
                    }
                }
                g_loggedRpaDiagnostic = true;
            }
        }

        const int matchIdx = findMatchingEnrollee(addr);
        if (matchIdx >= 0) {
            ++g_matchCount;
            Enrollee* e = g_store->getMutable(static_cast<size_t>(matchIdx));
            if (e != nullptr) {
                const uint32_t now = millis();
                e->lastSeenMs = now;
                e->lastRssi = rssi;
                if (e->smoothedRssi == 0.0f) {
                    e->smoothedRssi = static_cast<float>(rssi);
                } else {
                    e->smoothedRssi = (1.0f - kRssiEmaAlpha) * e->smoothedRssi +
                                        kRssiEmaAlpha * static_cast<float>(rssi);
                }
                e->present = true;
                Serial.printf("[scan] MATCH %s -> %s rssi=%d\n", addrStr, e->label, rssi);
            }
        }
    }
};

void disableControllerResolution() {
    esp_ble_gap_config_local_privacy(false);

    int bondCount = esp_ble_get_bond_device_num();
    if (bondCount <= 0) {
        return;
    }

    esp_ble_bond_dev_t bonds[16];
    int num = bondCount;
    if (num > 16) {
        num = 16;
    }
    if (esp_ble_get_bond_device_list(&num, bonds) != ESP_OK) {
        return;
    }

    for (int i = 0; i < num; ++i) {
        esp_ble_remove_bond_device(bonds[i].bond_key.pid_key.static_addr);
    }
    Serial.printf("[monitor] cleared %d controller bonds for raw RPA scan\n", num);
}
}  // namespace

MonitoringMode::MonitoringMode(EnrolleeStore& store) : store_(store) {}

void MonitoringMode::start() {
    if (active_) {
        return;
    }

    g_store = &store_;
    g_scanCount = 0;
    g_resolvableCount = 0;
    g_matchCount = 0;
    g_loggedRpaDiagnostic = false;
    store_.resetRuntimeState();

    if (!bleEnsureReady("LabPresence-Monitor")) {
        Serial.println("[monitor] BLE init failed");
        g_store = nullptr;
        return;
    }

    disableControllerResolution();

    BLEScan* scan = BLEDevice::getScan();
    if (scan == nullptr) {
        Serial.println("[monitor] BLE scan unavailable");
        g_store = nullptr;
        return;
    }

    scan->setAdvertisedDeviceCallbacks(new MonitorScanCallbacks(), true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);

    active_ = true;
    lastUiRefreshMs_ = millis();
    Serial.println("[monitor] passive scan started (duplicate filter OFF)");
    updateDisplay();
}

void MonitoringMode::stop() {
    if (!active_) {
        return;
    }

    bleShutdownForModeSwitch();
    g_store = nullptr;
    active_ = false;
}

void MonitoringMode::updatePresence() {
    const uint32_t now = millis();
    for (size_t i = 0; i < store_.count(); ++i) {
        Enrollee* e = store_.getMutable(i);
        if (e == nullptr) {
            continue;
        }
        if (e->lastSeenMs == 0) {
            e->present = false;
            continue;
        }
        e->present = (now - e->lastSeenMs) < kPresenceTimeoutMs;
    }
}

void MonitoringMode::tick() {
    if (!active_) {
        return;
    }

    updatePresence();

    const uint32_t now = millis();
    if (now - lastUiRefreshMs_ >= kUiRefreshMs) {
        lastUiRefreshMs_ = now;
        updateDisplay();
    }
}

void MonitoringMode::updateDisplay() {
    const uint32_t now = millis();
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(4, 4);
    display.println("Monitoring");

    display.setTextSize(1);
    display.setCursor(4, 30);
    display.printf("adv=%lu rpa=%lu match=%lu\n",
                    static_cast<unsigned long>(g_scanCount),
                    static_cast<unsigned long>(g_resolvableCount),
                    static_cast<unsigned long>(g_matchCount));

    int y = 52;
    for (size_t i = 0; i < store_.count(); ++i) {
        const Enrollee* e = store_.get(i);
        if (e == nullptr) {
            continue;
        }

        const bool present = e->present;
        display.setTextColor(present ? TFT_GREEN : TFT_RED, TFT_BLACK);
        display.setCursor(4, y);
        display.printf("%s %s\n", present ? "IN " : "OUT", e->label);

        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        if (e->lastSeenMs == 0) {
            display.setCursor(12, y + 12);
            display.println("never seen");
            y += 28;
            continue;
        }

        const uint32_t agoSec = (now - e->lastSeenMs) / 1000;
        display.setCursor(12, y + 12);
        display.printf("rssi=%d ema=%.0f ago=%lus\n",
                        e->lastRssi, e->smoothedRssi,
                        static_cast<unsigned long>(agoSec));
        y += 28;
        if (y > 210) {
            break;
        }
    }

    if (store_.count() == 0) {
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setCursor(4, 72);
        display.println("No enrollees.");
        display.setCursor(4, 86);
        display.println("BtnA: Enrollment");
    }

    display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display.setCursor(4, 220);
    display.println("BtnA: Enroll  BtnC: Clear");
}
