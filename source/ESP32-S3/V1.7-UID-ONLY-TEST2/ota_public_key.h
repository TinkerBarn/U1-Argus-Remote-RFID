#pragma once

// ESP32-S3 OTA public key. The matching private key remains local and must
// never be added to the repository.
static const uint8_t OTA_PUBLIC_KEY[] PROGMEM = R"KEY(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEPQKOiv8H3SzvfFC5NICsILzI38P8
ECPn+LVOmJsKSgRwFl6P8wGALmu/ywCMLaFtOUpV1Mzsn6xCWnPrJkwk2w==
-----END PUBLIC KEY-----
)KEY";
static const size_t OTA_PUBLIC_KEY_LEN = sizeof(OTA_PUBLIC_KEY);
