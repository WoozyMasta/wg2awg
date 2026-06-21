#ifndef AWG_BASE64_H
#define AWG_BASE64_H

#include <stddef.h>
#include <stdint.h>

/* Decode standard base64 into out. Returns decoded length, or -1 on error. */
int base64_decode(const char *in, size_t inlen, unsigned char *out,
                  size_t outmax);

/* Encode binary data to NUL-terminated standard base64.
 * out must have at least ((inlen + 2) / 3) * 4 + 1 bytes.
 * Returns number of base64 chars written (not counting NUL). */
int base64_encode(const uint8_t *in, size_t inlen, char *out);

#endif
