const express = require('express');
const http = require('http');
const socketIo = require('socket.io');
const cors = require('cors');
const path = require('path');
const { spawn } = require('child_process');
const readline = require('readline');

const app = express();
const server = http.createServer(app);
const io = socketIo(server, {
  cors: {
    origin: '*',
    methods: ['GET', 'POST']
  }
});

const LOG_ENGINE_STDERR = process.env.LOG_ENGINE_STDERR === '1';
const MAX_LOG_LINE = 512;
const BUILD_COMMIT =
  process.env.APP_COMMIT_SHA ||
  process.env.SOURCE_COMMIT ||
  process.env.RAILWAY_GIT_COMMIT_SHA ||
  process.env.RENDER_GIT_COMMIT ||
  process.env.GITHUB_SHA ||
  process.env.VERCEL_GIT_COMMIT_SHA ||
  'unknown';
const BUILD_TIME =
  process.env.APP_BUILD_TIME || process.env.BUILD_TIME || process.env.RAILWAY_DEPLOYMENT_CREATED_AT || 'unknown';
const BUILD_INFO = {
  version: '1.0.0',
  commit: BUILD_COMMIT,
  shortCommit: BUILD_COMMIT === 'unknown' ? 'unknown' : BUILD_COMMIT.slice(0, 12),
  buildTime: BUILD_TIME,
  nodeEnv: process.env.NODE_ENV || 'development'
};

function readPositiveNumber(name, fallback) {
  const parsed = Number(process.env[name]);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

const STREAM_INTERVAL_MS = readPositiveNumber('STREAM_INTERVAL_MS', 8);
const ENGINE_COMMAND_TIMEOUT_MS = readPositiveNumber('ENGINE_COMMAND_TIMEOUT_MS', 5000);
const streamStats = {
  startedAt: Date.now(),
  emitted: 0,
  lastDurationMs: 0,
  maxDurationMs: 0,
  errors: 0
};

function compactLogLine(value) {
  const line = String(value).replace(/\s+/g, ' ').trim();
  return line.length > MAX_LOG_LINE ? `${line.slice(0, MAX_LOG_LINE)}...` : line;
}

function compactError(error) {
  if (!error) {
    return 'unknown error';
  }
  return compactLogLine(error.message || error);
}

function sameDepthSide(left = [], right = []) {
  if (left.length !== right.length) {
    return false;
  }

  return left.every((level, index) => {
    const peer = right[index] || {};
    return (
      Number(level.price || 0) === Number(peer.price || 0) &&
      Number(level.quantity || 0) === Number(peer.quantity || 0) &&
      Number(level.order_count || 0) === Number(peer.order_count || 0)
    );
  });
}

function sameSnapshotDepth(left, right) {
  return sameDepthSide(left.bids, right.bids) && sameDepthSide(left.asks, right.asks);
}

app.use(cors());
app.use((req, res, next) => {
  res.setHeader('X-LFOB-Commit', BUILD_INFO.shortCommit);
  next();
});
app.use(express.static('public'));

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// API endpoint for current market data
app.get('/api/snapshot', async (req, res) => {
  const snapshot = await simulator.getOrderBookSnapshot();
  res.json({
    success: true,
    data: snapshot,
    timestamp: Date.now()
  });
});

// API endpoint for system info
app.get('/api/info', (req, res) => {
  res.json({
    success: true,
    system: {
      name: 'Lock-Free Order Book',
      version: '1.0.0',
      languages: ['Rust', 'C++', 'JavaScript'],
      features: ['Real-time data', 'Lock-free architecture', 'Dual-engine telemetry'],
      symbol: simulator.currentSymbol,
      description: 'Dual native matching engines streamed through a Socket.IO market gateway',
      build: BUILD_INFO
    }
  });
});

app.get('/api/build', (req, res) => {
  res.json({
    success: true,
    build: BUILD_INFO
  });
});

app.get('/api/stream-stats', (req, res) => {
  const uptimeSec = Math.max(0.001, (Date.now() - streamStats.startedAt) / 1000);
  res.json({
    success: true,
    stream: {
      targetIntervalMs: STREAM_INTERVAL_MS,
      emitted: streamStats.emitted,
      updatesPerSecond: streamStats.emitted / uptimeSec,
      lastDurationMs: streamStats.lastDurationMs,
      maxDurationMs: streamStats.maxDurationMs,
      errors: streamStats.errors
    },
    engines: {
      rust: simulator.rustEngine.perf,
      cpp: simulator.cppEngine.perf
    }
  });
});

class Engine {
  constructor(name, cmd, args) {
    this.name = name;
    this.stopping = false;
    this.process = spawn(cmd, args, { cwd: path.join(__dirname, '..') });
    this.rl = readline.createInterface({ input: this.process.stdout });

    this.batchResolvers = [];
    this.snapshotResolvers = [];
    this.latestSnapshot = { bids: [], asks: [] };
    this.lastBatch = null;
    this.perf = {
      ordersPerSec: 0,
      latency: 0,
      latencyNs: 0,
      latencyMinNs: 0,
      latencyMaxNs: 0,
      memory: 0,
      totalProcessed: 0,
      lastProcessed: 0,
      mismatches: 0
    };

    this.rl.on('line', (line) => {
      try {
        const data = JSON.parse(line);
        if (data.type === 'batch_result') {
          const latencyNs = Number(data.latency_ns || 0);
          const processed = Number(data.processed || 0);
          this.perf.latency = Math.floor(latencyNs / 1000);
          this.perf.latencyNs = latencyNs;
          this.perf.latencyMinNs = Number(data.latency_min_ns || latencyNs);
          this.perf.latencyMaxNs = Number(data.latency_max_ns || latencyNs);
          this.perf.lastProcessed = processed;
          this.perf.totalProcessed += processed;
          if (latencyNs > 0) {
            const currentOps = Math.floor((processed / latencyNs) * 1e9);
            this.perf.ordersPerSec = Math.floor(this.perf.ordersPerSec * 0.8 + currentOps * 0.2);
          }

          this.lastBatch = data;
          const resolve = this.batchResolvers.shift();
          if (resolve) {
            resolve(data);
          }
        } else if (data.type === 'snapshot') {
          this.latestSnapshot = data;
          const resolve = this.snapshotResolvers.shift();
          if (resolve) {
            resolve(data);
          }
        }
      } catch {
        console.error(`[${this.name}] parse error: ${compactLogLine(line)}`);
      }
    });

    this.process.stderr.on('data', (data) => {
      if (LOG_ENGINE_STDERR) {
        console.error(`[${this.name}] stderr: ${compactLogLine(data)}`);
      }
    });
    this.process.on('error', (error) => {
      console.error(`[${this.name}] spawn error: ${compactError(error)}`);
    });
    this.process.on('exit', (code, signal) => {
      if (!this.stopping && code !== 0) {
        console.error(`[${this.name}] exited code=${code} signal=${signal}`);
      }
    });
  }

  withTimeout(promise, label) {
    let timer;
    const timeout = new Promise((_, reject) => {
      timer = setTimeout(() => {
        reject(new Error(`${this.name} ${label} timed out after ${ENGINE_COMMAND_TIMEOUT_MS}ms`));
      }, ENGINE_COMMAND_TIMEOUT_MS);
    });
    return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
  }

  writeLine(payload) {
    const line = JSON.stringify(payload) + '\n';
    if (this.process.stdin.write(line)) {
      return Promise.resolve();
    }
    return new Promise((resolve) => this.process.stdin.once('drain', resolve));
  }

  async sendBatch(orders) {
    const result = new Promise((resolve) => {
      this.batchResolvers.push(resolve);
    });
    await this.writeLine({ action: 'batch', orders });
    return this.withTimeout(result, 'batch');
  }

  async requestSnapshot() {
    const result = new Promise((resolve) => {
      this.snapshotResolvers.push(resolve);
    });
    await this.writeLine({ action: 'snapshot' });
    return this.withTimeout(result, 'snapshot');
  }

  stop() {
    this.stopping = true;
    this.process.kill('SIGTERM');
  }
}

const MAGNIFICENT_7 = {
  AAPL: { name: 'Apple Inc.', basePrice: 175.0 },
  GOOGL: { name: 'Alphabet Inc.', basePrice: 140.0 },
  AMZN: { name: 'Amazon.com Inc.', basePrice: 145.0 },
  META: { name: 'Meta Platforms Inc.', basePrice: 320.0 },
  MSFT: { name: 'Microsoft Corporation', basePrice: 380.0 },
  NVDA: { name: 'NVIDIA Corporation', basePrice: 450.0 },
  TSLA: { name: 'Tesla Inc.', basePrice: 240.0 }
};

class Simulator {
  constructor() {
    this.orderIdCounter = 1;
    this.currentSymbol = 'AAPL';
    this.stockPrices = {};

    Object.keys(MAGNIFICENT_7).forEach((symbol) => {
      this.stockPrices[symbol] = MAGNIFICENT_7[symbol].basePrice;
    });

    this.metrics = {
      totalOrders: 0,
      totalTrades: 0,
      volume: 0,
      lastPrice: MAGNIFICENT_7[this.currentSymbol].basePrice,
      symbol: this.currentSymbol,
      companyName: MAGNIFICENT_7[this.currentSymbol].name
    };

    this.trades = [];
    this.snapshotQueue = Promise.resolve();

    // Spin up real engines
    this.rustEngine = new Engine('Rust', process.env.RUST_ENGINE_PATH || 'rust/target/release/order_book_rust', []);
    this.cppEngine = new Engine('C++', process.env.CPP_ENGINE_PATH || 'cpp/build/order_book_cpp', []);

    setInterval(() => this.updateAllPrices(), 15000);
  }

  switchStock(symbol) {
    if (MAGNIFICENT_7[symbol]) {
      this.currentSymbol = symbol;
      this.metrics.symbol = symbol;
      this.metrics.companyName = MAGNIFICENT_7[symbol].name;
      this.metrics.lastPrice = this.stockPrices[symbol];
      // Reset engines if we wanted realistic isolated stock states, but for now we share the book
    }
  }

  updateAllPrices() {
    const now = new Date();
    const timeBase = now.getMinutes() / 60;

    Object.keys(MAGNIFICENT_7).forEach((symbol) => {
      const basePrice = MAGNIFICENT_7[symbol].basePrice;
      const priceMultiplier = 1 + Math.sin(timeBase * Math.PI * 2) * 0.015;
      const randomVariation = (Math.random() - 0.5) * 0.01;
      this.stockPrices[symbol] = basePrice * (priceMultiplier + randomVariation);
      if (symbol === this.currentSymbol) {
        this.metrics.lastPrice = this.stockPrices[symbol];
      }
    });
  }

  generateOrderBatch() {
    const currentPrice = this.stockPrices[this.currentSymbol];
    const intensity = Math.random();
    const orderCount = intensity > 0.8 ? Math.floor(Math.random() * 50) + 10 : Math.floor(Math.random() * 10) + 5;

    const orders = [];
    for (let i = 0; i < orderCount; i++) {
      const side = Math.random() < 0.5 ? 'buy' : 'sell';
      const spread = currentPrice * 0.001;
      const priceVariation = (Math.random() - 0.5) * (currentPrice * 0.002);

      let price =
        side === 'buy' ? currentPrice - spread / 2 + priceVariation : currentPrice + spread / 2 + priceVariation;
      price = Math.round(price * 100) / 100;
      const quantity = Math.floor(Math.random() * 500) + 50;

      orders.push({
        id: this.orderIdCounter++,
        side,
        price,
        quantity,
        symbol: this.currentSymbol
      });
    }

    this.metrics.totalOrders += orders.length;

    // Simulate some trades for the UI (since the naive engines might not return trade events effectively via JSON yet)
    if (Math.random() > 0.7 && orders.length > 0) {
      const t = orders[0];
      const tradePrice = t.price;
      this.trades.push({
        id: Date.now(),
        price: tradePrice,
        quantity: t.quantity,
        timestamp: Date.now(),
        buyOrderId: t.id,
        sellOrderId: t.id + 1
      });
      this.metrics.totalTrades++;
      this.metrics.volume += t.quantity;
      this.metrics.lastPrice = tradePrice;
      if (this.trades.length > 50) this.trades.shift();
    }

    return orders;
  }

  async collectOrderBookSnapshot() {
    // Generate new orders
    const orders = this.generateOrderBatch();

    const [rustBatch, cppBatch] = await Promise.all([
      this.rustEngine.sendBatch(orders),
      this.cppEngine.sendBatch(orders)
    ]);

    const expectedProcessed = orders.length;
    if (Number(rustBatch.processed || 0) !== expectedProcessed) {
      this.rustEngine.perf.mismatches++;
    }
    if (Number(cppBatch.processed || 0) !== expectedProcessed) {
      this.cppEngine.perf.mismatches++;
    }
    if (Number(rustBatch.processed || 0) !== Number(cppBatch.processed || 0)) {
      this.rustEngine.perf.mismatches++;
      this.cppEngine.perf.mismatches++;
    }

    // Request snapshots
    const [rustSnap, cppSnap] = await Promise.all([
      this.rustEngine.requestSnapshot(),
      this.cppEngine.requestSnapshot()
    ]);

    // Use Rust's snapshot as the canonical one for the UI since they process the same data
    const bids = rustSnap.bids || [];
    const asks = rustSnap.asks || [];
    if (!sameSnapshotDepth(rustSnap, cppSnap)) {
      this.rustEngine.perf.mismatches++;
      this.cppEngine.perf.mismatches++;
    }

    return {
      bids,
      asks,
      trades: this.trades.slice(-10).reverse(),
      metrics: {
        ...this.metrics,
        rustPerformance: this.rustEngine.perf,
        cppPerformance: this.cppEngine.perf
      },
      stockInfo: {
        symbol: this.currentSymbol,
        name: MAGNIFICENT_7[this.currentSymbol].name,
        price: this.stockPrices[this.currentSymbol]
      }
    };
  }

  async getOrderBookSnapshot() {
    const snapshot = this.snapshotQueue.then(() => this.collectOrderBookSnapshot());
    this.snapshotQueue = snapshot.catch(() => {});
    return snapshot;
  }
}

const simulator = new Simulator();

// Pre-fill some data
for (let i = 0; i < 10; i++) simulator.generateOrderBatch();

io.on('connection', async (socket) => {
  socket.emit('orderbook-snapshot', await simulator.getOrderBookSnapshot());
  socket.on('switch-stock', async (symbol) => {
    simulator.switchStock(symbol);
    socket.emit('orderbook-snapshot', await simulator.getOrderBookSnapshot());
  });
});

async function streamMarketData() {
  const startedAt = Date.now();
  try {
    const snapshot = await simulator.getOrderBookSnapshot();
    io.emit('orderbook-update', snapshot);
    if (snapshot.trades && snapshot.trades.length > 0) {
      io.emit('new-trades', snapshot.trades);
    }
    streamStats.emitted++;
  } catch (error) {
    streamStats.errors++;
    console.error(`stream error: ${compactError(error)}`);
  } finally {
    const elapsed = Date.now() - startedAt;
    streamStats.lastDurationMs = elapsed;
    streamStats.maxDurationMs = Math.max(streamStats.maxDurationMs, elapsed);
    setTimeout(streamMarketData, Math.max(0, STREAM_INTERVAL_MS - elapsed));
  }
}

setTimeout(streamMarketData, STREAM_INTERVAL_MS);

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

function shutdown(signal) {
  console.log(`Received ${signal}; shutting down`);
  simulator.rustEngine.stop();
  simulator.cppEngine.stop();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 2000).unref();
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
