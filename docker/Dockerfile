# syntax=docker/dockerfile:1

# A small production base image for Varn apps. The builder compiles the release binary with the full
# core statically linked (TLS/OpenSSL, zip, HTTP, sockets, ffi, json, xml, datetime, logging), then the
# runtime stage ships only the stripped binary on a slim base — the toolchain and dependency sources
# stay in the builder and are thrown away, the way python:slim and node:slim keep just the runtime.
#
#   docker build -t varn:latest -f docker/Dockerfile .
#
# Then base your app on it:
#
#   FROM varn:latest
#   COPY app.lua /app/app.lua
#   CMD ["app.lua"]

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates pkg-config perl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# the default Linux build already turns on every core module; the flags are explicit so the image is
# self-documenting. the binary is stripped to drop debug symbols.
RUN cmake -B build -S . -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DVARN_ENABLE_TLS=ON \
        -DVARN_ENABLE_ZIP=ON \
        -DVARN_ENABLE_STATIC_FILES=ON \
    && cmake --build build --target varn -j"$(nproc)" \
    && strip build/bin/varn


# a slim runtime keeps the image small: only the 12 MB stripped binary plus the handful of shared
# libraries it links. trixie's glibc and libstdc++ are newer than the builder's, so the binary stays
# forward-compatible.
FROM debian:trixie-slim AS runtime

# ca-certificates verifies outbound TLS; libstdc++6/libgcc-s1 are the only shared libraries the binary
# links (everything else is static). a non-root user runs the app by default.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 varn

COPY --from=builder /src/build/bin/varn /usr/local/bin/varn

USER varn
WORKDIR /app
ENTRYPOINT ["varn"]
