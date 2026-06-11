#include "rpa_resolve.h"

#include <Arduino.h>
#include <cstring>

#include "mbedtls/aes.h"

namespace {
constexpr uint8_t kTestIrk[16] = {
    0x4C, 0x68, 0xFD, 0x7F, 0x07, 0xE0, 0xE0, 0x97,
    0x98, 0xD8, 0x96, 0x32, 0x63, 0x2A, 0xEC, 0xC7,
};
constexpr uint8_t kTestPrand[3] = {0x70, 0x88, 0x49};
constexpr uint8_t kTestHash[3] = {0x0E, 0xCA, 0x14};

void reverseCopy16(const uint8_t* src, uint8_t dst[16]) {
    for (size_t i = 0; i < 16; ++i) {
        dst[i] = src[15 - i];
    }
}

void extractPrand(const uint8_t rpa[6], RpaVariant variant, uint8_t prand[3]) {
    if (variant == RpaVariant::PrandReversed || variant == RpaVariant::BothReversed) {
        prand[0] = rpa[2];
        prand[1] = rpa[1];
        prand[2] = rpa[0];
        return;
    }

    prand[0] = rpa[0];
    prand[1] = rpa[1];
    prand[2] = rpa[2];
}

void prepareIrk(const uint8_t irk[16], RpaVariant variant, uint8_t prepared[16]) {
    if (variant == RpaVariant::IrkReversed || variant == RpaVariant::BothReversed) {
        reverseCopy16(irk, prepared);
        return;
    }

    memcpy(prepared, irk, 16);
}

const char* variantName(RpaVariant variant) {
    switch (variant) {
        case RpaVariant::Normal: return "normal";
        case RpaVariant::IrkReversed: return "irk-rev";
        case RpaVariant::PrandReversed: return "prand-rev";
        case RpaVariant::BothReversed: return "both-rev";
    }
    return "unknown";
}
}  // namespace

bool rpaAh(const uint8_t irk[16], const uint8_t prand[3], uint8_t hashOut[3]) {
    uint8_t plaintext[16] = {};
    plaintext[13] = prand[0];
    plaintext[14] = prand[1];
    plaintext[15] = prand[2];

    uint8_t ciphertext[16] = {};
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int rc = mbedtls_aes_setkey_enc(&ctx, irk, 128);
    if (rc != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    rc = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, plaintext, ciphertext);
    mbedtls_aes_free(&ctx);
    if (rc != 0) {
        return false;
    }

    hashOut[0] = ciphertext[13];
    hashOut[1] = ciphertext[14];
    hashOut[2] = ciphertext[15];
    return true;
}

bool rpaIsResolvable(const uint8_t addr[6]) {
    return (addr[0] & 0xC0) == 0x40;
}

bool rpaResolveVariant(const uint8_t rpa[6], const uint8_t irk[16], RpaVariant variant) {
    if (!rpaIsResolvable(rpa)) {
        return false;
    }

    uint8_t preparedIrk[16] = {};
    uint8_t prand[3] = {};
    prepareIrk(irk, variant, preparedIrk);
    extractPrand(rpa, variant, prand);

    const uint8_t receivedHash[3] = {rpa[3], rpa[4], rpa[5]};
    uint8_t localHash[3] = {};
    if (!rpaAh(preparedIrk, prand, localHash)) {
        return false;
    }

    return memcmp(localHash, receivedHash, 3) == 0;
}

bool rpaResolve(const uint8_t rpa[6], const uint8_t irk[16]) {
    return rpaResolveFindVariant(rpa, irk) >= 0;
}

int rpaResolveFindVariant(const uint8_t rpa[6], const uint8_t irk[16]) {
    for (int i = 0; i < 4; ++i) {
        if (rpaResolveVariant(rpa, irk, static_cast<RpaVariant>(i))) {
            return i;
        }
    }
    return -1;
}

void rpaNormalizeIrk(const uint8_t rawIrk[16], int variant, uint8_t normalizedIrk[16]) {
    if (variant == static_cast<int>(RpaVariant::IrkReversed) ||
        variant == static_cast<int>(RpaVariant::BothReversed)) {
        reverseCopy16(rawIrk, normalizedIrk);
        return;
    }

    memcpy(normalizedIrk, rawIrk, 16);
}

void rpaLogResolveDiagnostic(const uint8_t rpa[6], const uint8_t irk[16], const char* label) {
    Serial.printf("[rpa] diagnose %s rpa=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  label, rpa[0], rpa[1], rpa[2], rpa[3], rpa[4], rpa[5]);

    for (int i = 0; i < 4; ++i) {
        const auto variant = static_cast<RpaVariant>(i);
        uint8_t preparedIrk[16] = {};
        uint8_t prand[3] = {};
        prepareIrk(irk, variant, preparedIrk);
        extractPrand(rpa, variant, prand);

        uint8_t localHash[3] = {};
        if (!rpaAh(preparedIrk, prand, localHash)) {
            Serial.printf("[rpa]   %s: ah() failed\n", variantName(variant));
            continue;
        }

        Serial.printf("[rpa]   %s: calc=%02X%02X%02X recv=%02X%02X%02X %s\n",
                      variantName(variant),
                      localHash[0], localHash[1], localHash[2],
                      rpa[3], rpa[4], rpa[5],
                      rpaResolveVariant(rpa, irk, variant) ? "MATCH" : "miss");
    }
}

bool rpaRunSelfTest() {
    uint8_t hash[3] = {};
    if (!rpaAh(kTestIrk, kTestPrand, hash)) {
        Serial.println("[rpa] self-test: ah() failed");
        return false;
    }

    const bool ok = memcmp(hash, kTestHash, 3) == 0;
    if (!ok) {
        Serial.printf("[rpa] self-test: expected=%02X%02X%02X got=%02X%02X%02X\n",
                      kTestHash[0], kTestHash[1], kTestHash[2],
                      hash[0], hash[1], hash[2]);
    }
    return ok;
}
