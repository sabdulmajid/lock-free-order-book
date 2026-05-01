# Lock-Free Order Book (Rust & C++)

A high-performance, **lock-free limit order book** with dual engines implemented in **Rust** and **C++**. It features a **Node.js** orchestrator and a real-time web dashboard that streams live, identical market data to both engines, allowing for a **true side-by-side performance comparison** of Rust vs. C++.

## 🌟 The Project
This project was built to empirically evaluate the performance, safety, and latency characteristics of Rust against C++ in an ultra-low latency, concurrent environment.

**Unlike typical benchmarks, this project runs both engines simultaneously on the same hardware.**
1. A Node.js backend streams thousands of randomized "Magnificent 7" stock orders per second via `stdin` to both the Rust and C++ binary processes.
2. The natively compiled C++ and Rust engines utilize **lock-free data structures** (SPSC/MPSC queues, atomic operations) to parse JSON payloads, match trades, and manage the order book.
3. The engines report their instantaneous latency and throughput back to Node.js via `stdout`.
4. A React/Vanilla-JS frontend visualizes the live Order Book depth, spread, and the real-time speed/latency differences between C++ and Rust via WebSockets.

---

## 🚀 Live Demo & Deployment
This application is fully containerized and production-ready. 

**No login is required to view the dashboard!** If you run this locally, please ensure that port `3000` is free (or change the `PORT` env variable) to avoid conflicts with other local services like Open WebUI.

### Deploying to Render / Railway
This repository contains a `Dockerfile` and `railway.json` that automatically provisions an Ubuntu image, installs `g++`, `cmake`, `cargo`, and `nodejs`, builds both native binaries, and exposes the web dashboard.
Simply connect this repository to your preferred PaaS to deploy.

---

## 💻 Running Locally

### Prerequisites
- Node.js (v20+)
- Rust (`cargo`)
- C++11/23 Compiler (`g++` or `clang++`), `cmake`, and `make`

### 1. Build the C++ Engine
```bash
cd cpp
mkdir -p build && cd build
cmake ..
make order_book_cpp
```

### 2. Build the Rust Engine
```bash
cd rust
cargo build --release --bin order_book_rust
```

### 3. Start the Dashboard (Node.js Orchestrator)
```bash
cd web-dashboard
npm install
npm start
```

Visit `http://localhost:3000` to watch the engines battle it out!

---

## 📊 Formal Benchmarks
Both engines include rigorous standard benchmarks using `Google Benchmark` for C++ and `Criterion` for Rust. 

### Rust (`cargo bench`)
| Operation | Time / Throughput |
|-----------|-------------------|
| Insert 10K Orders | ~1.31 ms |
| Cancel 1K Orders | ~39.2 µs |
| Modify 1K Orders | ~99.1 µs |
| Match 100K Orders | ~4.00 ms |
| Concurrent SPSC (100K) | ~6.60 ms |
| Concurrent MPSC (4x50K) | ~14.4 ms |

### C++ (`./order_book_benches`)
| Operation | Time / Throughput |
|-----------|-------------------|
| Insert 10K Orders | ~1.35 ms |
| Cancel 1K Orders | ~1.98 ms |
| Modify 1K Orders | ~0.83 ms |
| Match 100K Orders | ~8.71 ms |
| Concurrent Processing | ~7.44 ms |

*Note: Results may vary based on CPU architecture. The live dashboard provides real-time comparative metrics.*

---

## 🏗️ Architecture Deep-Dive

```text
┌────────────────┐    WebSocket     ┌────────────────────┐
│  Web Dashboard │ ◄──────────────► │    Node.js App     │
│  (JavaScript)  │                  │  (Market Gateway)  │
└────────────────┘                  └────────┬───────────┘
                                             │ (stdin/stdout JSON streams)
                                  ┌──────────┴──────────┐
                                  ▼                     ▼
                        ┌───────────────────┐ ┌───────────────────┐
                        │    Rust Engine    │ │    C++ Engine     │
                        │ (Native Process)  │ │ (Native Process)  │
                        └───────────────────┘ └───────────────────┘
```

### Safety vs Speed
- **Rust**: Proves that zero-cost abstractions and memory safety (borrow checker) do not compromise high-frequency trading speeds. In fact, Rust's standard library and `BTreeMap` handle large match cycles incredibly efficiently.
- **C++20/23**: Highlights the raw control and ecosystem maturity, using `std::map` and custom manual memory allocations, optimized with `-O3`. It leverages modern **C++20/23 features** such as:
  - `<compare>` (spaceship operator `<=>`) for defaulted and extremely fast zero-cost struct comparisons.
  - `<ranges>` (`std::ranges::find_if`) for cleaner and more composable algorithmic pipelines.
  - `std::erase_if` to natively simplify the erase-remove idiom on Standard Library containers like `std::deque`.

---
*Built as a demonstrative tool for Systems Engineering and HFT.*