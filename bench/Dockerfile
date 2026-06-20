# linux toolchain + load generator + node/python baselines for the comparison benchmark.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates pkg-config \
        wrk nodejs python3 python3-pip \
    && pip3 install --break-system-packages --no-cache-dir uvicorn uvloop httptools \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /varn
