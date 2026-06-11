#pragma once

#include "enrollee.h"

class EnrollmentMode {
public:
    explicit EnrollmentMode(EnrolleeStore& store);
    void start();
    void stop();
    void updateDisplay();
    bool isActive() const { return active_; }

private:
    EnrolleeStore& store_;
    bool active_ = false;
};
