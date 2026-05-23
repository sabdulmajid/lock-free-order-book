# syntax=docker/dockerfile:1.7

ARG DEBIAN_RELEASE=trixie
ARG NODE_VERSION=22
ARG RUST_IMAGE=rust:1-slim-bookworm
ARG SOURCE_COMMIT=unknown
ARG BUILD_TIME=unknown

FROM debian:${DEBIAN_RELEASE}-slim AS cpp-build
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    ca-certificates \
    cmake \
    g++ \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

COPY cpp/CMakeLists.txt cpp/CMakeLists.txt
COPY cpp/src cpp/src
COPY cpp/third_party cpp/third_party

RUN --mount=type=cache,target=/src/cpp/build \
    cmake -S cpp -B cpp/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLFOB_BUILD_BENCHMARKS=OFF \
      -DLFOB_BUILD_TESTS=OFF \
      -DLFOB_ENABLE_NATIVE_ARCH=OFF \
    && cmake --build cpp/build --target order_book_cpp \
    && install -D cpp/build/order_book_cpp /out/cpp/build/order_book_cpp \
    && strip /out/cpp/build/order_book_cpp

FROM ${RUST_IMAGE} AS rust-build
WORKDIR /src/rust

RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY rust/Cargo.toml rust/Cargo.lock ./
COPY rust/benches benches
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    mkdir -p src \
    && printf 'fn main() {}' > src/main.rs \
    && printf '' > src/lib.rs \
    && \
    cargo fetch --locked

COPY rust/src src

RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/src/rust/target \
    cargo build --release --locked --bin order_book_rust \
    && install -D target/release/order_book_rust /out/rust/target/release/order_book_rust \
    && strip /out/rust/target/release/order_book_rust

FROM node:${NODE_VERSION}-${DEBIAN_RELEASE}-slim AS node-deps
WORKDIR /app/web-dashboard

COPY web-dashboard/package.json web-dashboard/package-lock.json ./
RUN --mount=type=cache,target=/root/.npm \
    npm ci --omit=dev --no-audit --no-fund

FROM node:${NODE_VERSION}-${DEBIAN_RELEASE}-slim AS runtime
ARG SOURCE_COMMIT=unknown
ARG BUILD_TIME=unknown

ENV NODE_ENV=production \
    PORT=3000 \
    STREAM_INTERVAL_MS=8 \
    APP_COMMIT_SHA=${SOURCE_COMMIT} \
    APP_BUILD_TIME=${BUILD_TIME} \
    CPP_ENGINE_PATH=/app/cpp/build/order_book_cpp \
    RUST_ENGINE_PATH=/app/rust/target/release/order_book_rust

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=cpp-build /out/cpp/build/order_book_cpp ./cpp/build/order_book_cpp
COPY --from=rust-build /out/rust/target/release/order_book_rust ./rust/target/release/order_book_rust
COPY --from=node-deps /app/web-dashboard/node_modules ./web-dashboard/node_modules
COPY web-dashboard/public ./web-dashboard/public
COPY web-dashboard/server.js web-dashboard/package.json ./web-dashboard/

USER node
WORKDIR /app/web-dashboard
EXPOSE 3000

CMD ["node", "server.js"]
