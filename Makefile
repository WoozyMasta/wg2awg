VERSION     ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
IMAGE_NAME  = wg2awg
BUILD_DIR   = build

SRCS = src/main.c src/proxy.c src/transform.c src/blake2s.c src/cps.c \
  src/fastrand.c src/base64.c src/log.c src/net_addr.c src/net_sock.c \
  src/curve25519.c src/config_file.c src/config_runtime.c src/session_table.c \
  src/obfs.c \
  src/proxy_io_batch.c src/proxy_emit.c src/proxy_s2c_client.c \
  src/proxy_s2c_gateway.c src/proxy_c2s_client.c src/proxy_c2s_gateway.c \
  src/proxy_reconnect.c src/proxy_startup.c src/proxy_control.c \
  src/proxy_shutdown.c src/proxy_init_io.c src/proxy_net.c \
  src/proxy_runtime.c

CFLAGS              = -O2 -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE \
                      -ffunction-sections -fdata-sections -flto \
                      -DVERSION=\"$(VERSION)\"
LDFLAGS             = -static -Wl,--gc-sections -flto -s -lpthread
TEST_SRCS           = src/blake2s.c src/cps.c src/transform.c src/fastrand.c \
                      src/base64.c src/log.c src/obfs.c
TEST_CFLAGS         = -g -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE
TEST_LDFLAGS        =
ASAN_TEST_CFLAGS    = $(TEST_CFLAGS) -O1 -fno-omit-frame-pointer \
                      -fsanitize=address
ASAN_TEST_LDFLAGS   = -fsanitize=address
UBSAN_TEST_CFLAGS   = $(TEST_CFLAGS) -O1 -fno-omit-frame-pointer \
                      -fsanitize=undefined -fno-sanitize-recover=all
UBSAN_TEST_LDFLAGS  = -fsanitize=undefined

SANITIZER_CC  ?= gcc
CLANG_FORMAT  ?= clang-format
CLANG_TIDY    ?= clang-tidy
FORMAT_SRCS   := $(filter-out src/curve25519_fiat32.c,$(wildcard src/*.c)) $(wildcard src/*.h)

# Platform map: suffix -> Docker platform string
PLATFORM_arm64    := linux/arm64
PLATFORM_arm      := linux/arm/v7
PLATFORM_armv5    := linux/arm/v5
PLATFORM_amd64    := linux/amd64
PLATFORM_mips     := linux/mips
PLATFORM_mipsel   := linux/mipsle
PLATFORM_mips64le := linux/mips64le
PLATFORM_ppc64le  := linux/ppc64le
PLATFORM_386      := linux/386

MUSLCC_TAG_arm64    := aarch64-linux-musl
MUSLCC_TAG_arm      := arm-linux-musleabihf
MUSLCC_TAG_armv5    := arm-linux-musleabi
MUSLCC_TAG_amd64    := x86_64-linux-musl
MUSLCC_TAG_mips     := mips-linux-musl
MUSLCC_TAG_mipsel   := mipsel-linux-musl
MUSLCC_TAG_mips64le := mips64el-linux-musl
MUSLCC_TAG_ppc64le  := powerpc64le-linux-musl
MUSLCC_TAG_386      := i486-linux-musl

ALL_ARCHS := arm64 arm armv5 amd64 mips mipsel mips64le ppc64le 386
CONTAINER_TEST_IMAGE ?= wg2awg-test
CONTAINER_TEST_TARGET ?= test
CONTAINER_TEST_RUNTIME_ARCHS ?= amd64 arm64 arm armv5
CONTAINER_TEST_ARCHS ?= $(CONTAINER_TEST_RUNTIME_ARCHS)
CONTAINER_TEST_MUSLCC_TAG ?= x86_64-linux-musl
CONTAINER_CHECK_IMAGE ?= wg2awg-check
CONTAINER_CHECK_TARGET ?= check-local

# Generate an explicit phony rule for each architecture
define CONTAINER_BUILD_RULE
.PHONY: container-build-$(1)
container-build-$(1):
	@mkdir -p $(BUILD_DIR)
	bash build.sh $$(PLATFORM_$(1)) $(1) $$(MUSLCC_TAG_$(1)) \
		$$(VERSION) $$(BUILD_DIR)
endef
$(foreach arch,$(ALL_ARCHS),$(eval $(call CONTAINER_BUILD_RULE,$(arch))))

.PHONY: clean build container-build-matrix

clean:
	rm -f $(BUILD_DIR)/$(IMAGE_NAME) $(BUILD_DIR)/$(IMAGE_NAME)-*

build:
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BUILD_DIR)/$(IMAGE_NAME) $(SRCS)

container-build-matrix: $(addprefix container-build-,$(ALL_ARCHS))

.PHONY: check-local container-check container-test container-check-image container-lint
.PHONY: container-test-matrix

check-local: fmt-check lint test test-hardening test-stress
	@echo "Local CI checks passed"

container-check-image:
	docker build -t $(CONTAINER_CHECK_IMAGE) -f Dockerfile.check --no-cache \
		--build-arg CHECK_TARGET="$(CONTAINER_CHECK_TARGET)" .

container-lint: container-check-image

container-test:
	docker build -t $(CONTAINER_TEST_IMAGE) -f Dockerfile.test --no-cache \
		--build-arg TEST_TARGET="$(CONTAINER_TEST_TARGET)" \
		--build-arg MUSLCC_TAG="$(CONTAINER_TEST_MUSLCC_TAG)" .

container-check:
	$(MAKE) container-check-image

define CONTAINER_TEST_RULE
.PHONY: container-test-$(1)
container-test-$(1):
	docker buildx inspect --bootstrap >/dev/null
	docker buildx build \
		--platform $$(PLATFORM_$(1)) \
		-f Dockerfile.test \
		--build-arg TEST_TARGET=$$(CONTAINER_TEST_TARGET) \
		--build-arg MUSLCC_TAG=$$(MUSLCC_TAG_$(1)) \
		--output type=cacheonly \
		.
endef
$(foreach arch,$(ALL_ARCHS),$(eval $(call CONTAINER_TEST_RULE,$(arch))))

container-test-matrix: $(addprefix container-test-,$(CONTAINER_TEST_ARCHS))

.PHONY: fmt fmt-check lint

fmt:
	$(CLANG_FORMAT) -i $(FORMAT_SRCS)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SRCS)

lint:
	$(CLANG_TIDY) -quiet -header-filter='^src/' $(SRCS) -- -std=c11 -D_GNU_SOURCE -Isrc

.PHONY: test test-stress test-integration test-asan test-ubsan test-hardening
.PHONY: test-blake2s test-cps test-transform test-base64 test-session \
        test-curve25519 test-config-file test-config-runtime test-obfs test-net-addr \
        test-net-sock test-session-table test-net-addr-fuzz test-cps-fuzz

test: test-blake2s test-cps test-transform test-base64 test-session test-curve25519 test-config-file test-config-runtime test-obfs test-net-addr test-net-sock test-session-table
	@echo "All tests passed"

test-blake2s: src/test_blake2s.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_blake2s $^
	/tmp/test_blake2s

test-cps: src/test_cps.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_cps $^
	/tmp/test_cps

test-transform: src/test_transform.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_transform $^
	/tmp/test_transform

test-base64: src/test_base64.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_base64 $^
	/tmp/test_base64

test-session: src/test_session.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_session $^
	/tmp/test_session

test-curve25519: src/test_curve25519.c src/curve25519.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_curve25519 $^
	/tmp/test_curve25519

test-config-file: src/test_config_file.c src/config_file.c src/curve25519.c $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_config_file $^
	/tmp/test_config_file

test-config-runtime: src/test_config_runtime.c src/config_runtime.c src/transform.h
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_config_runtime src/test_config_runtime.c src/config_runtime.c src/base64.c src/cps.c src/fastrand.c src/obfs.c
	/tmp/test_config_runtime

test-obfs: src/test_obfs.c src/obfs.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_obfs src/test_obfs.c src/obfs.c
	/tmp/test_obfs

test-net-addr: src/test_net_addr.c src/net_addr.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_net_addr src/test_net_addr.c src/net_addr.c
	/tmp/test_net_addr

test-net-sock: src/test_net_sock.c src/net_sock.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_net_sock src/test_net_sock.c src/net_sock.c
	/tmp/test_net_sock

test-session-table: src/test_session_table.c src/session_table.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_session_table src/test_session_table.c src/session_table.c
	/tmp/test_session_table

test-stress: src/test_stress.c build
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -lpthread -o /tmp/test_stress src/test_stress.c
	/tmp/test_stress

test-integration: src/test_stress.c build
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -lpthread -o /tmp/test_stress src/test_stress.c
	AWG_STRESS_ONLY="client_burst,gateway_bidirectional,server_multiclient" /tmp/test_stress

test-net-addr-fuzz: src/test_net_addr_fuzz.c src/net_addr.c
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_net_addr_fuzz src/test_net_addr_fuzz.c src/net_addr.c
	/tmp/test_net_addr_fuzz

test-cps-fuzz: src/test_cps_fuzz.c src/cps.c src/fastrand.c src/transform.h
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o /tmp/test_cps_fuzz src/test_cps_fuzz.c src/cps.c src/fastrand.c
	/tmp/test_cps_fuzz

test-asan:
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	$(MAKE) CC="$(SANITIZER_CC)" TEST_CFLAGS="$(ASAN_TEST_CFLAGS)" TEST_LDFLAGS="$(ASAN_TEST_LDFLAGS)" \
	test-blake2s test-transform test-config-runtime test-net-addr test-session-table test-curve25519

test-ubsan:
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	$(MAKE) CC="$(SANITIZER_CC)" TEST_CFLAGS="$(UBSAN_TEST_CFLAGS)" TEST_LDFLAGS="$(UBSAN_TEST_LDFLAGS)" \
	test-blake2s test-transform test-config-runtime test-net-addr test-session-table test-curve25519

test-hardening: test-net-addr-fuzz test-cps-fuzz test-asan test-ubsan test-integration
	@echo "Hardening tests passed"

.PHONY: release-notes

release-notes:
	@awk '\
	/^<!--/,/^-->/ { next } \
	/^## \[[0-9]+\.[0-9]+\.[0-9]+\]/ { if (found) exit; found=1; next } \
	found { \
		if (/^## \[/) { exit } \
		if (/^$$/) { flush(); print; next } \
		if (/^\* / || /^- /) { flush(); buf=$$0; next } \
		if (/^###/ || /^\[/) { flush(); print; next } \
		sub(/^[ \t]+/, ""); sub(/[ \t]+$$/, ""); \
		if (buf != "") { buf = buf " " $$0 } else { buf = $$0 } \
		next \
	} \
	function flush() { if (buf != "") { print buf; buf = "" } } \
	END { flush() } \
	' CHANGELOG.md
