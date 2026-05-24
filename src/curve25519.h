#ifndef AWG_CURVE25519_H
#define AWG_CURVE25519_H

#include <stdint.h>

/* Derive Curve25519 public key from private key (clamped internally). */
void curve25519_public_key(uint8_t pk[32], const uint8_t sk[32]);

#endif
