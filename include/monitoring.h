#pragma once

#include "enrollee.h"

class MonitoringMode {
public:
    explicit MonitoringMode(EnrolleeStore& store);
    void start();
    void stop();
    void tick();
    void updateDisplay();
    bool isActive() const { return active_; }

private:
    EnrolleeStore& store_;
    bool active_ = false;
    uint32_t lastUiRefreshMs_ = 0;

    void updatePresence();
};
