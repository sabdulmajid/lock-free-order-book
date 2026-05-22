# Web Dashboard

The dashboard is a Socket.IO-driven institutional-style terminal for the dual-engine order book. It renders a compact instrument strip, 20-level depth, spread state, 32-print time-and-sales tape, Rust/C++ throughput and latency telemetry, parity counters, and price/performance canvases.

## Runtime Contract

`server.js` owns the market simulator, starts the Rust and C++ binaries, sends each engine identical JSON-line batches over stdin, reads latency and snapshot responses from stdout, and broadcasts normalized state to connected browsers. The stream loop targets an 8 ms cadence by default and schedules the next tick only after the current native-engine round trip completes, preventing websocket backlog when a local machine cannot sustain peak cadence.

The browser client keeps rendering work off the socket callback path. Incoming deltas update an in-memory state object, then one pending `requestAnimationFrame` refresh writes into pooled DOM rows and canvas contexts. Depth and trade rows are allocated once at startup; hot updates mutate `textContent`, class names, and canvas pixels only.

## Throughput Guardrails

| Layer              | Mechanism                                                      | Production Constraint                                                                           |
| ------------------ | -------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| Native process I/O | JSON-line stdin/stdout protocol with bounded command timeouts  | Engines must answer batch and snapshot requests before the next websocket emission is queued.   |
| Parity accounting  | Rust/C++ processed-count and depth comparison counters         | Format compatibility is checked without mutating the historical dashboard payload shape.        |
| WebSocket loop     | Single in-flight snapshot chain and cadence-aware rescheduling | Slow native rounds reduce emission frequency instead of accumulating unbounded pending updates. |
| Browser rendering  | `requestAnimationFrame`, preallocated rows, and canvas traces  | Socket callbacks only replace state; the frame callback performs DOM and canvas writes.         |
| Runtime logging    | Compact server errors and opt-in native stderr forwarding      | Production logs retain health deltas without streaming verbose local engine diagnostics.        |

## Local Run

Build both native engines before starting the dashboard:

```bash
cd ../cpp
cmake -S . -B build
cmake --build build --target order_book_cpp

cd ../rust
cargo build --release --bin order_book_rust

cd ../web-dashboard
npm install
npm start
```

Open `http://localhost:3000`.

## Production Runtime

The container entrypoint runs `node server.js` directly and reads the listener port from `PORT`, with `3000` only as a local fallback. Native engine paths are configurable through `CPP_ENGINE_PATH` and `RUST_ENGINE_PATH`; the production image sets them to the stripped binaries copied from the C++ and Rust build stages. `STREAM_INTERVAL_MS` defaults to `8`, `ENGINE_COMMAND_TIMEOUT_MS` defaults to `5000`, and native stderr is suppressed unless `LOG_ENGINE_STDERR=1` is explicitly set.

## Socket Events

Client to server:

| Event          | Payload       | Purpose                                  |
| -------------- | ------------- | ---------------------------------------- |
| `switch-stock` | Symbol string | Changes the active simulated instrument. |

Server to client:

| Event                | Payload                                    | Purpose                                             |
| -------------------- | ------------------------------------------ | --------------------------------------------------- |
| `orderbook-snapshot` | Full book, metrics, trades, stock metadata | Initial state after connect or symbol switch.       |
| `orderbook-update`   | Full top-of-book state and metrics         | Streaming update from the simulator/native engines. |
| `new-trades`         | Recent trade array                         | Tape updates derived from simulated executions.     |

## Rendering Constraints

The UI intentionally avoids headers, footers, marketing panels, charting libraries, drop shadows, and card wrappers. Layout is a fixed terminal grid with 1 px neutral borders, monochrome-first typography, limited bid/ask accents, and stable row heights. Canvas traces replace Chart.js to avoid animation work and object churn under constant websocket deltas.

## Data Shape

```json
{
  "bids": [{ "price": 100.1, "quantity": 1200, "order_count": 4 }],
  "asks": [{ "price": 100.2, "quantity": 900, "order_count": 3 }],
  "trades": [{ "price": 100.15, "quantity": 200, "timestamp": 1710000000000 }],
  "metrics": {
    "totalOrders": 100000,
    "totalTrades": 320,
    "volume": 450000,
    "lastPrice": 100.15,
    "rustPerformance": { "ordersPerSec": 1000000, "latency": 40, "memory": 0 },
    "cppPerformance": { "ordersPerSec": 1200000, "latency": 35, "memory": 0 }
  },
  "stockInfo": { "symbol": "AAPL", "name": "Apple Inc.", "price": 175.12 }
}
```
