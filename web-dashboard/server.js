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
    origin: "*",
    methods: ["GET", "POST"]
  }
});

app.use(cors());
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
      name: "Lock-Free Order Book",
      version: "1.0.0",
      languages: ["Rust", "C++", "JavaScript"],
      features: ["Real-time data", "Lock-free architecture", "Educational content"],
      symbol: simulator.currentSymbol,
      description: "High-performance order book with real market data integration"
    }
  });
});

class Engine {
  constructor(name, cmd, args) {
    this.name = name;
    this.process = spawn(cmd, args, { cwd: path.join(__dirname, '..') });
    this.rl = readline.createInterface({ input: this.process.stdout });
    
    this.snapshotCb = null;
    this.latestSnapshot = { bids: [], asks: [] };
    this.perf = {
      ordersPerSec: 0,
      latency: 0,
      memory: 0,
      totalProcessed: 0
    };

    this.rl.on('line', (line) => {
      try {
        const data = JSON.parse(line);
        if (data.type === 'batch_result') {
          // Update perf metrics (simplistic moving average)
          this.perf.latency = Math.floor(data.latency_ns / 1000); // microseconds
          this.perf.totalProcessed += data.processed;
          // Orders per sec estimate based on recent batch
          if (data.latency_ns > 0) {
            const currentOps = Math.floor((data.processed / data.latency_ns) * 1e9);
            this.perf.ordersPerSec = Math.floor(this.perf.ordersPerSec * 0.8 + currentOps * 0.2);
          }
        } else if (data.type === 'snapshot') {
          this.latestSnapshot = data;
          if (this.snapshotCb) {
            this.snapshotCb(data);
            this.snapshotCb = null;
          }
        }
      } catch(e) {
        console.error(`[${this.name}] parse error:`, line);
      }
    });

    this.process.stderr.on('data', data => console.error(`[${this.name}] stderr:`, data.toString()));
  }

  sendBatch(orders) {
    this.process.stdin.write(JSON.stringify({ action: 'batch', orders }) + '\n');
  }

  requestSnapshot() {
    return new Promise(resolve => {
      this.snapshotCb = resolve;
      this.process.stdin.write(JSON.stringify({ action: 'snapshot' }) + '\n');
    });
  }
}

const MAGNIFICENT_7 = {
  'AAPL': { name: 'Apple Inc.', basePrice: 175.0 },
  'GOOGL': { name: 'Alphabet Inc.', basePrice: 140.0 },
  'AMZN': { name: 'Amazon.com Inc.', basePrice: 145.0 },
  'META': { name: 'Meta Platforms Inc.', basePrice: 320.0 },
  'MSFT': { name: 'Microsoft Corporation', basePrice: 380.0 },
  'NVDA': { name: 'NVIDIA Corporation', basePrice: 450.0 },
  'TSLA': { name: 'Tesla Inc.', basePrice: 240.0 }
};

class Simulator {
  constructor() {
    this.orderIdCounter = 1;
    this.currentSymbol = 'AAPL';
    this.stockPrices = {};
    
    Object.keys(MAGNIFICENT_7).forEach(symbol => {
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

    // Spin up real engines
    this.rustEngine = new Engine('Rust', 'rust/target/release/order_book_rust', []);
    this.cppEngine = new Engine('C++', 'cpp/build/order_book_cpp', []);

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
    
    Object.keys(MAGNIFICENT_7).forEach(symbol => {
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
      
      let price = side === 'buy' ? currentPrice - spread/2 + priceVariation : currentPrice + spread/2 + priceVariation;
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

  async getOrderBookSnapshot() {
    // Generate new orders
    const orders = this.generateOrderBatch();
    
    // Send to both engines
    this.rustEngine.sendBatch(orders);
    this.cppEngine.sendBatch(orders);

    // Request snapshots
    const [rustSnap, cppSnap] = await Promise.all([
      this.rustEngine.requestSnapshot(),
      this.cppEngine.requestSnapshot()
    ]);

    // Use Rust's snapshot as the canonical one for the UI since they process the same data
    const bids = rustSnap.bids || [];
    const asks = rustSnap.asks || [];

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
}

const simulator = new Simulator();

// Pre-fill some data
for(let i=0; i<10; i++) simulator.generateOrderBatch();

io.on('connection', async (socket) => {
  socket.emit('orderbook-snapshot', await simulator.getOrderBookSnapshot());
  socket.on('switch-stock', async (symbol) => {
    simulator.switchStock(symbol);
    socket.emit('orderbook-snapshot', await simulator.getOrderBookSnapshot());
  });
});

setInterval(async () => {
  const snapshot = await simulator.getOrderBookSnapshot();
  io.emit('orderbook-update', snapshot);
  if (snapshot.trades && snapshot.trades.length > 0) {
    io.emit('new-trades', snapshot.trades);
  }
}, 100);

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});