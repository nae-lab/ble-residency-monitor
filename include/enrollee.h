#pragma once

#include <cstdint>
#include <cstddef>

static constexpr size_t kMaxEnrollees = 8;
static constexpr size_t kLabelMaxLen = 16;

struct Enrollee {
    char label[kLabelMaxLen];
    uint8_t irk[16];
    uint8_t identityAddr[6];
    bool present;
    uint32_t lastSeenMs;
    int lastRssi;
    float smoothedRssi;
};

class EnrolleeStore {
public:
    bool begin();
    size_t count() const { return count_; }
    const Enrollee* get(size_t index) const;
    Enrollee* getMutable(size_t index);
    bool add(const char* label, const uint8_t irk[16], const uint8_t identityAddr[6]);
    void clear();
    void resetRuntimeState();

private:
    Enrollee enrollees_[kMaxEnrollees];
    size_t count_ = 0;
    bool loaded_ = false;

    void load();
    void save();
};
