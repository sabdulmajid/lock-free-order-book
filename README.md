# Lock-Free Order Book

Dual native matching engines, implemented in Rust and modern C++, are driven by a Node.js market gateway and rendered through a dense browser terminal. The system is built for latency experiments around bounded-depth books, lock-free queue mechanics, process-local matching, and real-time comparative telemetry.

The C++ engine uses a C++23 baseline with feature-gated C++26 experiments where the active compiler and standard library expose them. Standard-library gaps are isolated behind local compatibility adapters for `expected`, `inplace_vector`, print emission, and atomic min/max watermarks, keeping the hot paths stable on older compilers while allowing newer production toolchains to exercise the native facilities.

## Runtime Topology

```text
Browser terminal
  Socket.IO stream
Node.js market gateway
  stdin/stdout JSON-line protocol
Rust engine                 C++ engine
BTreeMap book               bounded flat price levels
Criterion benches           Google Benchmark benches
```

The gateway generates identical order flow for both native processes, records batch latency and throughput from each engine, and broadcasts depth, trade, and performance state to the dashboard. The dashboard batches websocket deltas behind `requestAnimationFrame`, reuses preallocated DOM rows for book and tape views, and renders price/performance traces on canvas.

## Build And Run

Prerequisites: Node.js 20 or newer, Rust with Cargo, CMake 3.20 or newer, and a C++ compiler with C++23 mode support.

```bash
cd cpp
cmake -S . -B build
cmake --build build --target order_book_cpp

cd ../rust
cargo build --release --bin order_book_rust

cd ../web-dashboard
npm install
npm start
```

Open `http://localhost:3000`. The default port can be overridden with `PORT`.

## Benchmarks

```bash
cd cpp
cmake --build build --target order_book_benches concurrent_queue_benches concurrent_order_book_benches
./build/order_book_benches
./build/concurrent_queue_benches
./build/concurrent_order_book_benches

cd ../rust
cargo bench
```

Benchmark results depend on CPU topology, compiler, standard library, and `-march=native` code generation. Treat the checked-in benchmark harnesses as the authoritative measurement path rather than the historical numbers from older revisions.

## Architectural Matrix

| Standard / Facility          | Applied Feature                                                      | Modified Component                                                                          | Mechanical Latency / Hardware Benefit                                                                                                     |
| ---------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| C++20                        | Concepts for order payloads, execution channels, and gateway parsers | `cpp/src/low_latency.h`, `cpp/src/concurrent_queue.h`, `cpp/src/main.cpp`                   | Rejects invalid template wiring at compile time and removes runtime interface checks from queue and gateway paths.                        |
| C++20                        | `std::hardware_destructive_interference_size` cache-line isolation   | `cpp/src/concurrent_queue.h`                                                                | Places enqueue and dequeue cursors on separate destructive-interference lines to reduce false sharing under producer/consumer contention. |
| C++20                        | `std::jthread` and `std::stop_token` lifecycle control               | `cpp/benches/concurrent_queue_benches.cpp`, `cpp/benches/concurrent_order_book_benches.cpp` | Uses RAII joins and cooperative cancellation in concurrent benchmark producers.                                                           |
| C++20                        | `std::span` and `std::string_view` parser surface                    | `cpp/src/stdin_parser.h`, `cpp/src/main.cpp`                                                | Tokenizes stdin JSON-line messages without allocating owned field substrings.                                                             |
| C++20                        | Defaulted three-way comparison                                       | `cpp/src/order.h`, `cpp/src/trade.h`                                                        | Keeps price-time keys and engine value types comparable with compiler-generated ordering.                                                 |
| C++23 / compatibility        | `expected`-style parser and validation flow                          | `cpp/src/low_latency.h`, `cpp/src/stdin_parser.h`, `cpp/src/order_book.cpp`                 | Removes exception propagation from request parsing and order admission.                                                                   |
| C++23 / compatibility        | Print-path abstraction over `std::println` when available            | `cpp/src/low_latency.h`, `cpp/src/main.cpp`                                                 | Avoids iostream output on stdout telemetry and uses the standard print facility on capable libraries.                                     |
| C++23 / vendor availability  | Bounded flat price-level storage                                     | `cpp/src/order_book.h`, `cpp/src/order_book.cpp`                                            | Uses contiguous sorted storage for top-of-book levels, with the adapter ready for `std::flat_map` when the library ships it.              |
| C++23                        | `[[assume]]` hot-path invariant hints                                | `cpp/src/order_book.cpp`, `cpp/src/low_latency.h`                                           | Communicates validated quantity invariants to the optimizer on match loops.                                                               |
| C++26 / compatibility        | `inplace_vector`-style fixed-capacity batches                        | `cpp/src/low_latency.h`, `cpp/src/order_book.h`, `cpp/src/order_book.cpp`                   | Keeps trade batches and depth snapshots stack resident with no hot-path vector growth.                                                    |
| C++26-inspired compatibility | Hazard-pointer-style deferred reclamation                            | `cpp/src/low_latency.h`, `cpp/src/order_book.cpp`                                           | Defers retired order-node reclamation until active reader hazard slots are clear.                                                         |
| C++26 / compatibility        | Atomic fetch min/max watermarks                                      | `cpp/src/low_latency.h`, `cpp/src/main.cpp`                                                 | Maintains latency extrema with lock-free compare-exchange fallback until standard min/max atomics are available.                          |

## Dashboard

The web dashboard is a dark, high-density terminal: one instrument strip, live depth, spread, time-and-sales, Rust/C++ telemetry, and canvas traces. It intentionally avoids generic page templates, shadows, decorative cards, marketing copy, and external charting runtime overhead.

## Deployment

The provided `Dockerfile` is a multi-stage production image. It builds C++ on Debian Trixie with `g++`, CMake, Ninja, `-O3`, portable architecture defaults, and detected C++26 experimental standard flags where the compiler accepts them; builds Rust in a separate Cargo stage; installs production-only Node.js dependencies; strips the native binaries; and copies only runtime artifacts into a `node:22-trixie-slim` final image. Local benchmark builds can opt into host-specific code generation with `-DLFOB_ENABLE_NATIVE_ARCH=ON`.

Production URL: `https://lock-free-order-book.onrender.com`

The deployed service exposes `GET /api/build` and an `X-LFOB-Commit` response header so release audits can compare the served commit against GitHub.

```bash
DOCKER_BUILDKIT=1 docker build \
  --build-arg SOURCE_COMMIT="$(git rev-parse HEAD)" \
  --build-arg BUILD_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  -t lock-free-order-book .
docker run --rm -p 3000:3000 -e PORT=3000 lock-free-order-book
```

Runtime environment variables:

| Variable                    | Default                                              | Purpose                                                                          |
| --------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------- |
| `PORT`                      | `3000`                                               | HTTP and Socket.IO listener port, compatible with Render/Railway port injection. |
| `STREAM_INTERVAL_MS`        | `8`                                                  | Target market-data emission interval.                                            |
| `ENGINE_COMMAND_TIMEOUT_MS` | `5000`                                               | Native engine batch/snapshot response timeout.                                   |
| `APP_COMMIT_SHA`            | `unknown`                                            | Optional served commit identifier for `/api/build` and `X-LFOB-Commit`.          |
| `APP_BUILD_TIME`            | `unknown`                                            | Optional UTC build timestamp for deployment audits.                              |
| `CPP_ENGINE_PATH`           | `/app/cpp/build/order_book_cpp` in Docker            | C++ engine executable path.                                                      |
| `RUST_ENGINE_PATH`          | `/app/rust/target/release/order_book_rust` in Docker | Rust engine executable path.                                                     |
| `LOG_ENGINE_STDERR`         | unset                                                | Set to `1` only when native stderr diagnostics are needed.                       |
