# Lock-Free Order Book

Dual native matching engines, implemented in Rust and modern C++, driven by a Node.js market gateway and rendered through a dense browser terminal. The system is built for latency experiments around bounded-depth books, lock-free queue mechanics, process-local matching, and real-time comparative telemetry. Check out the hosted project [here](https://lock-free-order-book.onrender.com/)!

The C++ engine uses a C++23 baseline with feature-gated C++26 experiments where the active compiler and standard library expose them. Standard-library gaps are isolated behind local compatibility adapters for `expected`, `inplace_vector`, print emission, and atomic min/max watermarks, keeping the hot paths stable on older compilers while allowing newer production toolchains to exercise the native facilities.

## Runtime Topology

```text
        Browser terminal (opens Socket.IO stream)
                          ↓
Node.js market gateway (uses stdin/stdout JSON-line protocol)
         ↓                           ↓
    Rust engine                 C++ engine
(Criterion benches)       (Google Benchmark benches)
```

The gateway generates identical order flow for both native processes, records batch latency and throughput from each engine, and broadcasts depth, trade, and performance state to the dashboard. The dashboard batches websocket deltas behind `requestAnimationFrame`, reuses preallocated DOM rows for book and tape views, and renders price/performance traces on canvas.

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

## Deployment

The provided `Dockerfile` is a multi-stage production image. It builds C++ on Debian Trixie with `g++`, CMake, Ninja, `-O3`, portable architecture defaults, and detected C++26 experimental standard flags where the compiler accepts them; builds Rust in a separate Cargo stage; installs production-only Node.js dependencies; strips the native binaries; and copies only runtime artifacts into a `node:22-trixie-slim` final image. Local benchmark builds can opt into host-specific code generation with `-DLFOB_ENABLE_NATIVE_ARCH=ON`.

Production URL: `https://lock-free-order-book.onrender.com`

The deployed service exposes `GET /api/build` and an `X-LFOB-Commit` response header so release audits can compare the served commit against GitHub.

You can run it locally with docker:
```bash
DOCKER_BUILDKIT=1 docker build \
  --build-arg SOURCE_COMMIT="$(git rev-parse HEAD)" \
  --build-arg BUILD_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  -t lock-free-order-book .
docker run --rm -p 3000:3000 -e PORT=3000 lock-free-order-book
```

## Architectural Matrix (Experimental C++ Features)
These are some new features in C++ that I wanted to try (found in C++20, 23, and 26)

| Standard                     | Applied Feature                                                      | Benefit                                                                                                                                   |
| ---------------------------- | -------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| C++20                        | Concepts for order payloads, execution channels, and gateway parsers | Rejects invalid template wiring at compile time and removes runtime interface checks from queue and gateway paths.                        |
| C++20                        | `std::hardware_destructive_interference_size` cache-line isolation   | Places enqueue and dequeue cursors on separate destructive-interference lines to reduce false sharing under producer/consumer contention. |
| C++20                        | `std::jthread` and `std::stop_token` lifecycle control               | Uses RAII joins and cooperative cancellation in concurrent benchmark producers.                                                           |
| C++20                        | `std::span` and `std::string_view` parser surface                    | Tokenizes stdin JSON-line messages without allocating owned field substrings.                                                             |
| C++20                        | Defaulted three-way comparison                                       | Keeps price-time keys and engine value types comparable with compiler-generated ordering.                                                 |
| C++23                        | `expected`-style parser and validation flow                          | Removes exception propagation from request parsing and order admission.                                                                   |
| C++23                        | Print-path abstraction over `std::println` when available            | Avoids iostream output on stdout telemetry and uses the standard print facility on capable libraries.                                     |
| C++23                        | Bounded flat price-level storage                                     | Uses contiguous sorted storage for top-of-book levels, with the adapter ready for `std::flat_map` when the library ships it.              |
| C++23                        | `[[assume]]` hot-path invariant hints                                | Communicates validated quantity invariants to the optimizer on match loops.                                                               |
| C++26                        | `inplace_vector`-style fixed-capacity batches                        | Keeps trade batches and depth snapshots stack resident with no hot-path vector growth.                                                    |
| C++26                        | Hazard-pointer-style deferred reclamation                            | Defers retired order-node reclamation until active reader hazard slots are clear.                                                         |
| C++26                        | Atomic fetch min/max watermarks                                      | Maintains latency extrema with lock-free compare-exchange fallback until standard min/max atomics are available.                          |
