#include "enrollee.h"

#include <Preferences.h>
#include <cstring>

namespace {
constexpr const char* kPrefsNamespace = "presence";
constexpr const char* kCountKey = "count";

void copyLabel(char* dst, const char* src) {
    strncpy(dst, src, kLabelMaxLen - 1);
    dst[kLabelMaxLen - 1] = '\0';
}

bool irkIsNonZero(const uint8_t irk[16]) {
    for (size_t i = 0; i < 16; ++i) {
        if (irk[i] != 0) {
            return true;
        }
    }
    return false;
}
}  // namespace

bool EnrolleeStore::begin() {
    load();
    loaded_ = true;
    return true;
}

const Enrollee* EnrolleeStore::get(size_t index) const {
    if (index >= count_) {
        return nullptr;
    }
    return &enrollees_[index];
}

Enrollee* EnrolleeStore::getMutable(size_t index) {
    if (index >= count_) {
        return nullptr;
    }
    return &enrollees_[index];
}

bool EnrolleeStore::add(const char* label, const uint8_t irk[16], const uint8_t identityAddr[6]) {
    if (!irkIsNonZero(irk)) {
        return false;
    }
    if (count_ >= kMaxEnrollees) {
        return false;
    }

    Enrollee& e = enrollees_[count_];
    copyLabel(e.label, label);
    memcpy(e.irk, irk, 16);
    memcpy(e.identityAddr, identityAddr, 6);
    e.present = false;
    e.lastSeenMs = 0;
    e.lastRssi = 0;
    e.smoothedRssi = 0.0f;
    ++count_;
    save();
    return true;
}

void EnrolleeStore::clear() {
    count_ = 0;
    memset(enrollees_, 0, sizeof(enrollees_));
    save();
}

void EnrolleeStore::resetRuntimeState() {
    for (size_t i = 0; i < count_; ++i) {
        enrollees_[i].present = false;
        enrollees_[i].lastSeenMs = 0;
        enrollees_[i].lastRssi = 0;
        enrollees_[i].smoothedRssi = 0.0f;
    }
}

void EnrolleeStore::load() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) {
        return;
    }

    count_ = prefs.getUChar(kCountKey, 0);
    if (count_ > kMaxEnrollees) {
        count_ = 0;
    }

    for (size_t i = 0; i < count_; ++i) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "e%zu", i);

        Enrollee& e = enrollees_[i];
        String label = prefs.getString((String(prefix) + "label").c_str(), "");
        copyLabel(e.label, label.c_str());

        String irkHex = prefs.getString((String(prefix) + "irk").c_str(), "");
        if (irkHex.length() == 32) {
            for (size_t b = 0; b < 16; ++b) {
                char byteStr[3] = {irkHex[b * 2], irkHex[b * 2 + 1], '\0'};
                e.irk[b] = static_cast<uint8_t>(strtoul(byteStr, nullptr, 16));
            }
        }

        String addrHex = prefs.getString((String(prefix) + "addr").c_str(), "");
        if (addrHex.length() == 12) {
            for (size_t b = 0; b < 6; ++b) {
                char byteStr[3] = {addrHex[b * 2], addrHex[b * 2 + 1], '\0'};
                e.identityAddr[b] = static_cast<uint8_t>(strtoul(byteStr, nullptr, 16));
            }
        }

        e.present = false;
        e.lastSeenMs = 0;
        e.lastRssi = 0;
        e.smoothedRssi = 0.0f;
    }

    prefs.end();
}

void EnrolleeStore::save() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return;
    }

    prefs.putUChar(kCountKey, static_cast<uint8_t>(count_));

    for (size_t i = 0; i < count_; ++i) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "e%zu", i);
        const Enrollee& e = enrollees_[i];

        prefs.putString((String(prefix) + "label").c_str(), e.label);

        char irkHex[33];
        for (size_t b = 0; b < 16; ++b) {
            snprintf(irkHex + b * 2, 3, "%02X", e.irk[b]);
        }
        prefs.putString((String(prefix) + "irk").c_str(), irkHex);

        char addrHex[13];
        for (size_t b = 0; b < 6; ++b) {
            snprintf(addrHex + b * 2, 3, "%02X", e.identityAddr[b]);
        }
        prefs.putString((String(prefix) + "addr").c_str(), addrHex);
    }

    prefs.end();
}
