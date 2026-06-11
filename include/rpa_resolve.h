#pragma once

#include <cstdint>

enum class RpaVariant : uint8_t {
    Normal = 0,
    IrkReversed = 1,
    PrandReversed = 2,
    BothReversed = 3,
};

bool rpaIsResolvable(const uint8_t addr[6]);
bool rpaAh(const uint8_t irk[16], const uint8_t prand[3], uint8_t hashOut[3]);
bool rpaResolveVariant(const uint8_t rpa[6], const uint8_t irk[16], RpaVariant variant);
bool rpaResolve(const uint8_t rpa[6], const uint8_t irk[16]);
int rpaResolveFindVariant(const uint8_t rpa[6], const uint8_t irk[16]);
void rpaNormalizeIrk(const uint8_t rawIrk[16], int variant, uint8_t normalizedIrk[16]);
void rpaLogResolveDiagnostic(const uint8_t rpa[6], const uint8_t irk[16], const char* label);
bool rpaRunSelfTest();
