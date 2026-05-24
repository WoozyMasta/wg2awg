ARG MUSLCC_TAG=x86_64-linux-musl
FROM --platform=$BUILDPLATFORM docker.io/muslcc/x86_64:$MUSLCC_TAG AS build

ARG VERSION=dev

# hadolint ignore=DL3018
RUN apk add --no-cache make

WORKDIR /src
COPY src/ src/
COPY Makefile .

RUN set -eux; \
    make build VERSION="$VERSION" CC=gcc BUILD_DIR=/out; \
    ! readelf -d /out/wg2awg 2>/dev/null | grep -q '(NEEDED)'; \
    mkdir -p /out/etc; \
    touch /out/etc/resolv.conf

FROM scratch
COPY --from=build /out/wg2awg /wg2awg
COPY --from=build /out/etc/resolv.conf /etc/resolv.conf
ENTRYPOINT ["/wg2awg"]
