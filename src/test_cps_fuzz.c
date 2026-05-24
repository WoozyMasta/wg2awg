#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "cps.h"

static uint32_t rng_state = 0x9E3779B9u;

static uint32_t rng32(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

static void append(char *dst, size_t n, const char *s) {
    size_t have = strlen(dst);
    size_t add = strlen(s);
    if (have + add + 1 >= n)
        return;
    memcpy(dst + have, s, add + 1);
}

static void test_cps_parse_generate_fuzz_style(void) {
    for (int i = 0; i < 2000; i++) {
        char spec[512] = {0};
        int segments = 1 + (int)(rng32() % 8);
        int expected_len = 0;

        for (int s = 0; s < segments; s++) {
            int kind = (int)(rng32() % 5);
            char part[96];
            part[0] = '\0';
            if (kind == 0) {
                int sz = 1 + (int)(rng32() % 64);
                snprintf(part, sizeof(part), "<r %d>", sz);
                expected_len += sz;
            } else if (kind == 1) {
                int sz = 1 + (int)(rng32() % 64);
                snprintf(part, sizeof(part), "<rc %d>", sz);
                expected_len += sz;
            } else if (kind == 2) {
                int sz = 1 + (int)(rng32() % 64);
                snprintf(part, sizeof(part), "<rd %d>", sz);
                expected_len += sz;
            } else if (kind == 3) {
                snprintf(part, sizeof(part), "<t>");
                expected_len += 4;
            } else {
                snprintf(part, sizeof(part), "<c>");
                expected_len += 4;
            }
            if (s > 0)
                append(spec, sizeof(spec), " ");
            append(spec, sizeof(spec), part);
        }

        cps_template_t tmpl;
        ASSERT_EQ(cps_parse(spec, &tmpl), 0);
        ASSERT_EQ(cps_max_size(&tmpl), expected_len);

        uint8_t buf[1500];
        int got = cps_generate(&tmpl, (uint32_t)i, buf, sizeof(buf));
        ASSERT_EQ(got, expected_len);
    }
}

static void test_cps_rejects_random_garbage(void) {
    char garb[128];
    cps_template_t tmpl;

    for (int i = 0; i < 1500; i++) {
        int len = 1 + (int)(rng32() % 80);
        for (int j = 0; j < len; j++) {
            int c = 32 + (int)(rng32() % 95);
            garb[j] = (char)c;
        }
        garb[len] = '\0';
        if (strchr(garb, '<') == NULL || strchr(garb, '>') == NULL) {
            ASSERT_EQ(cps_parse(garb, &tmpl), -1);
        }
    }
}

int main(void) {
    fprintf(stderr, "=== cps fuzz-style tests ===\n");
    RUN_TEST(cps_parse_generate_fuzz_style);
    RUN_TEST(cps_rejects_random_garbage);
    TEST_MAIN_END();
}
