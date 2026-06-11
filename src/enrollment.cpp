#include "enrollment.h"
#include "ble_lifecycle.h"
#include "rpa_resolve.h"

#include <M5Unified.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

namespace {
constexpr const char* kDeviceName = "LabPresence-Enroll";
constexpr const char* kServiceUuid = "0000fff0-0000-1000-8000-00805f9b34fb";
constexpr const char* kCharUuid = "0000fff1-0000-1000-8000-00805f9b34fb";

EnrolleeStore* g_store = nullptr;
bool g_bondCaptured = false;
char g_lastLabel[kLabelMaxLen] = {};
uint8_t g_lastIrk[16] = {};

void formatAddr(const uint8_t addr[6], char* out, size_t outLen) {
    snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

bool irkIsNonZero(const uint8_t irk[16]) {
    for (size_t i = 0; i < 16; ++i) {
        if (irk[i] != 0) {
            return true;
        }
    }
    return false;
}

void copyLabel(char* dst, const char* src) {
    strncpy(dst, src, kLabelMaxLen - 1);
    dst[kLabelMaxLen - 1] = '\0';
}

void configureSecurityParams() {
    BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

bool captureLatestBond(const uint8_t peerAddr[6]) {
    if (g_store == nullptr) {
        return false;
    }

    int bondCount = esp_ble_get_bond_device_num();
    if (bondCount <= 0) {
        return false;
    }

    esp_ble_bond_dev_t bonds[16];
    int num = bondCount;
    if (num > 16) {
        num = 16;
    }
    esp_err_t err = esp_ble_get_bond_device_list(&num, bonds);
    if (err != ESP_OK || num <= 0) {
        Serial.printf("[enroll] bond list read failed: %d\n", err);
        return false;
    }

    const esp_ble_bond_dev_t* bond = &bonds[num - 1];
    for (int i = 0; i < num; ++i) {
        if (memcmp(bonds[i].bd_addr, peerAddr, 6) == 0) {
            bond = &bonds[i];
            break;
        }
    }

    const uint8_t* rawIrk = bond->bond_key.pid_key.irk;
    if (!irkIsNonZero(rawIrk)) {
        Serial.println("[enroll] IRK is all zeros");
        return false;
    }

    uint8_t normalizedIrk[16] = {};
    const int variant = rpaResolveFindVariant(peerAddr, rawIrk);
    if (variant >= 0) {
        rpaNormalizeIrk(rawIrk, variant, normalizedIrk);
        Serial.printf("[enroll] peer RPA verified at bond time (variant=%d)\n", variant);
    } else {
        memcpy(normalizedIrk, rawIrk, 16);
        Serial.println("[enroll] WARN: IRK cannot resolve peer RPA at bond time");
        rpaLogResolveDiagnostic(peerAddr, rawIrk, "bond-time");
    }

    char label[kLabelMaxLen];
    snprintf(label, sizeof(label), "User%zu", g_store->count() + 1);

    if (!g_store->add(label, normalizedIrk, bond->bond_key.pid_key.static_addr)) {
        Serial.println("[enroll] failed to persist enrollee");
        return false;
    }

    copyLabel(g_lastLabel, label);
    memcpy(g_lastIrk, normalizedIrk, 16);
    g_bondCaptured = true;

    char addrStr[18];
    formatAddr(bond->bond_key.pid_key.static_addr, addrStr, sizeof(addrStr));
    char peerStr[18];
    formatAddr(peerAddr, peerStr, sizeof(peerStr));
    Serial.printf("[enroll] captured IRK for %s identity=%s peer=%s\n",
                  label, addrStr, peerStr);
    Serial.printf("[enroll] IRK=%02X%02X%02X%02X%02X%02X%02X%02X..."
                  "%02X%02X%02X%02X%02X%02X%02X%02X\n",
                  normalizedIrk[0], normalizedIrk[1], normalizedIrk[2], normalizedIrk[3],
                  normalizedIrk[4], normalizedIrk[5], normalizedIrk[6], normalizedIrk[7],
                  normalizedIrk[8], normalizedIrk[9], normalizedIrk[10], normalizedIrk[11],
                  normalizedIrk[12], normalizedIrk[13], normalizedIrk[14], normalizedIrk[15]);

    // Remove controller bond so monitoring scan receives raw RPAs.
    uint8_t bondAddr[6];
    memcpy(bondAddr, bond->bond_key.pid_key.static_addr, 6);
    esp_ble_remove_bond_device(bondAddr);
    return true;
}

class EnrollServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        Serial.println("[enroll] central connected");
        esp_bd_addr_t remoteAddr = {};
        memcpy(remoteAddr, param->connect.remote_bda, sizeof(remoteAddr));
        server->requestConnParams(remoteAddr, 0x10, 0x20, 0, 400);
        esp_ble_set_encryption(remoteAddr, ESP_BLE_SEC_ENCRYPT_MITM);
    }

    void onDisconnect(BLEServer* server) override {
        Serial.println("[enroll] central disconnected");
        BLEDevice::startAdvertising();
    }
};

class EnrollCharCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* characteristic) override {
        Serial.println("[enroll] encrypted read requested");
        (void)characteristic;
    }
};

class EnrollSecurityCallbacks : public BLESecurityCallbacks {
    void onAuthenticationComplete(esp_ble_auth_cmpl_t authCmplt) override {
        if (authCmplt.success) {
            Serial.println("[enroll] bonding complete");
            captureLatestBond(authCmplt.bd_addr);
        } else {
            Serial.printf("[enroll] bonding failed: %u\n", authCmplt.fail_reason);
        }
    }
};
}  // namespace

EnrollmentMode::EnrollmentMode(EnrolleeStore& store) : store_(store) {}

void EnrollmentMode::start() {
    if (active_) {
        return;
    }

    g_store = &store_;
    g_bondCaptured = false;
    g_lastLabel[0] = '\0';
    memset(g_lastIrk, 0, sizeof(g_lastIrk));

    if (!bleEnsureReady(kDeviceName)) {
        Serial.println("[enroll] BLE init failed");
        return;
    }

    configureSecurityParams();
    BLEDevice::setSecurityCallbacks(new EnrollSecurityCallbacks());

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new EnrollServerCallbacks());

    BLEService* service = server->createService(kServiceUuid);
    BLECharacteristic* characteristic = service->createCharacteristic(
        kCharUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    characteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    characteristic->setCallbacks(new EnrollCharCallbacks());
    characteristic->setValue("pair-me");
    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advData.setName(kDeviceName);
    advData.setCompleteServices(BLEUUID((uint16_t)0xFFF0));
    advertising->setAdvertisementData(advData);

    BLEAdvertisementData scanData;
    scanData.setName(kDeviceName);
    advertising->setScanResponseData(scanData);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);
    advertising->setScanFilter(false, false);
    BLEDevice::startAdvertising();

    active_ = true;
    Serial.printf("[enroll] advertising as %s (connectable)\n", kDeviceName);
    updateDisplay();
}

void EnrollmentMode::stop() {
    if (!active_) {
        return;
    }

    bleShutdownForModeSwitch();
    g_store = nullptr;
    active_ = false;
}

void EnrollmentMode::updateDisplay() {
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(4, 4);
    display.println("Enrollment");

    display.setTextSize(1);
    display.setCursor(4, 36);
    display.println("Use LightBlue or nRF");
    display.setCursor(4, 50);
    display.println("NOT iOS Settings BT");

    display.setCursor(4, 72);
    display.printf("Enrolled: %zu/%zu\n", store_.count(), kMaxEnrollees);

    if (g_bondCaptured) {
        display.setTextColor(TFT_GREEN, TFT_BLACK);
        display.setCursor(4, 96);
        display.printf("OK: %s\n", g_lastLabel);
        display.setCursor(4, 110);
        display.printf("IRK %02X%02X%02X%02X\n",
                        g_lastIrk[0], g_lastIrk[1], g_lastIrk[2], g_lastIrk[3]);
    } else {
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setCursor(4, 96);
        display.println("Waiting for bond...");
    }

    display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display.setCursor(4, 220);
    display.println("BtnB: Monitor  BtnC: Clear");
}
