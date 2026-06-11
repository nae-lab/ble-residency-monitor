#include <Arduino.h>
#include <M5Unified.h>

#include "app.h"
#include "enrollee.h"
#include "enrollment.h"
#include "monitoring.h"
#include "rpa_resolve.h"

namespace {
EnrolleeStore g_store;
EnrollmentMode g_enrollment(g_store);
MonitoringMode g_monitoring(g_store);
AppMode g_mode = AppMode::Idle;

void showIdleScreen() {
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(4, 4);
    display.println("BLE Presence");
    display.setTextSize(1);
    display.setCursor(4, 36);
    display.printf("Enrollees: %zu\n", g_store.count());
    display.setCursor(4, 56);
    display.println("BtnA: Enrollment");
    display.setCursor(4, 70);
    display.println("BtnB: Monitoring");
    display.setCursor(4, 84);
    display.println("BtnC: Clear enrollees");
}

void switchMode(AppMode next) {
    if (g_mode == AppMode::Enrollment) {
        g_enrollment.stop();
    } else if (g_mode == AppMode::Monitoring) {
        g_monitoring.stop();
    }

    g_mode = next;

    if (g_mode == AppMode::Enrollment) {
        g_enrollment.start();
    } else if (g_mode == AppMode::Monitoring) {
        g_monitoring.start();
    } else {
        showIdleScreen();
    }
}

void handleButtons() {
    if (M5.BtnA.wasPressed()) {
        switchMode(AppMode::Enrollment);
        return;
    }
    if (M5.BtnB.wasPressed()) {
        switchMode(AppMode::Monitoring);
        return;
    }
    if (M5.BtnC.wasPressed()) {
        if (g_mode != AppMode::Idle) {
            switchMode(AppMode::Idle);
        }
        g_store.clear();
        Serial.println("[app] enrollee list cleared");
        showIdleScreen();
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    auto cfg = M5.config();
    M5.begin(cfg);

    g_store.begin();

    Serial.println();
    Serial.println("=== LabPresence prototype (Case C) ===");
    if (rpaRunSelfTest()) {
        Serial.println("[rpa] ah() self-test PASSED");
    } else {
        Serial.println("[rpa] ah() self-test FAILED");
    }
    Serial.printf("[app] loaded %zu enrollee(s)\n", g_store.count());

    showIdleScreen();
}

void loop() {
    M5.update();
    handleButtons();

    if (g_mode == AppMode::Monitoring) {
        g_monitoring.tick();
    } else if (g_mode == AppMode::Enrollment && g_enrollment.isActive()) {
        static uint32_t lastEnrollUiMs = 0;
        const uint32_t now = millis();
        if (now - lastEnrollUiMs >= 500) {
            lastEnrollUiMs = now;
            g_enrollment.updateDisplay();
        }
    }

    delay(10);
}
