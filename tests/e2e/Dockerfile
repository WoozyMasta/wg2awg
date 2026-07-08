ARG GO_VERSION=1.26
ARG DEBIAN_VERSION=trixie

FROM docker.io/library/golang:$GO_VERSION-$DEBIAN_VERSION AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends pkg-config libmnl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src/wireguard-go
RUN git clone --depth=1 https://git.zx2c4.com/wireguard-go . && \
    make -j"$(nproc)"

WORKDIR /src/amneziawg-go
RUN git clone --depth=1 https://github.com/amnezia-vpn/amneziawg-go . && \
    make -j"$(nproc)"

WORKDIR /src/amneziawg-tools
RUN git clone --depth=1 https://github.com/amnezia-vpn/amneziawg-tools .

WORKDIR /src/amneziawg-tools/src
RUN make -j"$(nproc)"

WORKDIR /src/wg2awg
COPY src/ src/
COPY Makefile .
RUN make build

FROM docker.io/library/debian:$DEBIAN_VERSION

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        wireguard-tools iproute2 iputils-ping bash iperf3 tcpdump; \
    apt-get clean; \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /src/wireguard-go/wireguard-go /usr/local/bin/wireguard-go
COPY --from=build /src/amneziawg-go/amneziawg-go /usr/local/bin/amneziawg-go
COPY --from=build /src/amneziawg-tools/src/wg /usr/local/bin/awg
COPY --from=build /src/wg2awg/build/wg2awg /usr/local/bin/wg2awg
