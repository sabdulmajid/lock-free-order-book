class TerminalDashboard {
  constructor() {
    this.socket = io();
    this.depthLimit = 20;
    this.tradeLimit = 32;
    this.historyLimit = 240;
    this.lastRenderedOrderCount = 0;
    this.lastThroughputTime = performance.now();
    this.lastTradePrice = 0;
    this.pendingFrame = false;
    this.stats = {
      socketUpdates: 0,
      renderedFrames: 0,
      lastRenderMs: 0,
      maxRenderMs: 0
    };
    window.__terminalStats = this.stats;

    this.state = {
      bids: [],
      asks: [],
      trades: [],
      metrics: {},
      stockInfo: null
    };

    this.nodes = this.bindNodes();
    this.askRows = this.createRows(this.nodes.asksList, this.depthLimit, 'book-row', ['price', 'size', 'notional']);
    this.bidRows = this.createRows(this.nodes.bidsList, this.depthLimit, 'book-row', ['price', 'size', 'notional']);
    this.tradeRows = this.createRows(this.nodes.tradesList, this.tradeLimit, 'trade-row', [
      'trade-time',
      'trade-price',
      'trade-size'
    ]);
    this.tradeKeys = new Set();
    this.priceHistory = this.createSeries(this.historyLimit);
    this.perfHistory = {
      rustOps: this.createSeries(this.historyLimit),
      cppOps: this.createSeries(this.historyLimit),
      rustLatency: this.createSeries(this.historyLimit),
      cppLatency: this.createSeries(this.historyLimit)
    };

    this.setupEvents();
    this.setupSockets();
    this.startThroughputClock();
    this.queueRender();
  }

  bindNodes() {
    return {
      selector: document.getElementById('stock-selector'),
      connectionDot: document.getElementById('connection-status'),
      connectionText: document.getElementById('connection-text'),
      currentPrice: document.getElementById('current-price'),
      totalOrders: document.getElementById('total-orders'),
      totalTrades: document.getElementById('total-trades'),
      volume: document.getElementById('volume'),
      throughput: document.getElementById('throughput'),
      winner: document.getElementById('performance-winner'),
      speedDifference: document.getElementById('speed-difference'),
      spread: document.getElementById('spread'),
      asksList: document.getElementById('asks-list'),
      bidsList: document.getElementById('bids-list'),
      tradesList: document.getElementById('trades-list'),
      rustOrders: document.getElementById('rust-orders-sec'),
      rustLatency: document.getElementById('rust-latency'),
      rustMemory: document.getElementById('rust-memory'),
      cppOrders: document.getElementById('cpp-orders-sec'),
      cppLatency: document.getElementById('cpp-latency'),
      cppMemory: document.getElementById('cpp-memory'),
      priceSamples: document.getElementById('price-sample-count'),
      priceCanvas: document.getElementById('price-chart'),
      perfCanvas: document.getElementById('perf-chart')
    };
  }

  setupEvents() {
    this.nodes.selector.addEventListener('change', (event) => {
      this.socket.emit('switch-stock', event.target.value);
    });

    window.addEventListener('resize', () => this.queueRender(), { passive: true });
  }

  setupSockets() {
    this.socket.on('connect', () => {
      this.nodes.connectionDot.className = 'status-dot connected';
      this.nodes.connectionText.textContent = 'CONNECTED';
    });

    this.socket.on('disconnect', () => {
      this.nodes.connectionDot.className = 'status-dot disconnected';
      this.nodes.connectionText.textContent = 'DISCONNECTED';
    });

    this.socket.on('orderbook-snapshot', (data) => this.ingestSnapshot(data));
    this.socket.on('orderbook-update', (data) => this.ingestSnapshot(data));
    this.socket.on('new-trades', (trades) => this.ingestTrades(trades));
  }

  createRows(container, count, rowClass, cellClasses) {
    const fragment = document.createDocumentFragment();
    const rows = [];

    for (let i = 0; i < count; i += 1) {
      const row = document.createElement('div');
      row.className = rowClass;
      const cells = cellClasses.map((className) => {
        const cell = document.createElement('span');
        cell.className = className;
        row.appendChild(cell);
        return cell;
      });
      fragment.appendChild(row);
      rows.push({ row, cells });
    }

    container.appendChild(fragment);
    return rows;
  }

  createSeries(limit) {
    return {
      values: new Float64Array(limit),
      limit,
      cursor: 0,
      count: 0
    };
  }

  pushSeries(series, value) {
    series.values[series.cursor] = value;
    series.cursor = (series.cursor + 1) % series.limit;
    series.count = Math.min(series.count + 1, series.limit);
  }

  readSeries(series, index) {
    if (index >= series.count) {
      return 0;
    }
    const start = (series.cursor + series.limit - series.count) % series.limit;
    return series.values[(start + index) % series.limit];
  }

  ingestSnapshot(data) {
    this.stats.socketUpdates += 1;
    this.state.bids = Array.isArray(data.bids) ? data.bids : [];
    this.state.asks = Array.isArray(data.asks) ? data.asks : [];
    this.state.metrics = data.metrics || {};
    this.state.stockInfo = data.stockInfo || null;
    if (Array.isArray(data.trades)) {
      this.ingestTrades(data.trades, false);
    }
    this.sampleMetrics();
    this.queueRender();
  }

  ingestTrades(trades, schedule = true) {
    if (!Array.isArray(trades) || trades.length === 0) {
      return;
    }

    for (const trade of trades) {
      const key = `${trade.timestamp}:${trade.price}:${trade.quantity}:${trade.buyOrderId || ''}:${trade.sellOrderId || ''}`;
      if (this.tradeKeys.has(key)) {
        continue;
      }
      this.tradeKeys.add(key);
      this.state.trades.unshift({ ...trade, key });
    }
    while (this.state.trades.length > this.tradeLimit) {
      const removed = this.state.trades.pop();
      if (removed) {
        this.tradeKeys.delete(removed.key);
      }
    }

    if (schedule) {
      this.queueRender();
    }
  }

  sampleMetrics() {
    const metrics = this.state.metrics;
    const price = Number(metrics.lastPrice || this.state.stockInfo?.price || 0);
    if (price > 0) {
      this.pushSeries(this.priceHistory, price);
    }

    const rustOps = Number(metrics.rustPerformance?.ordersPerSec || 0);
    const cppOps = Number(metrics.cppPerformance?.ordersPerSec || 0);
    const rustLatency = Number(metrics.rustPerformance?.latency || 0);
    const cppLatency = Number(metrics.cppPerformance?.latency || 0);
    this.pushSeries(this.perfHistory.rustOps, rustOps);
    this.pushSeries(this.perfHistory.cppOps, cppOps);
    this.pushSeries(this.perfHistory.rustLatency, rustLatency);
    this.pushSeries(this.perfHistory.cppLatency, cppLatency);
  }

  queueRender() {
    if (this.pendingFrame) {
      return;
    }
    this.pendingFrame = true;
    requestAnimationFrame(() => {
      this.pendingFrame = false;
      this.render();
    });
  }

  render() {
    const startedAt = performance.now();
    this.renderInstrument();
    this.renderDepth();
    this.renderTrades();
    this.drawPrice();
    this.drawPerformance();
    const elapsed = performance.now() - startedAt;
    this.stats.renderedFrames += 1;
    this.stats.lastRenderMs = elapsed;
    this.stats.maxRenderMs = Math.max(this.stats.maxRenderMs, elapsed);
  }

  renderInstrument() {
    const metrics = this.state.metrics;
    const stockInfo = this.state.stockInfo;

    if (stockInfo) {
      this.nodes.selector.value = stockInfo.symbol;
      this.nodes.currentPrice.textContent = this.formatPrice(stockInfo.price);
    }

    this.nodes.totalOrders.textContent = this.formatInt(metrics.totalOrders || 0);
    this.nodes.totalTrades.textContent = this.formatInt(metrics.totalTrades || 0);
    this.nodes.volume.textContent = this.formatInt(metrics.volume || 0);

    const rust = metrics.rustPerformance;
    const cpp = metrics.cppPerformance;
    if (rust) {
      this.nodes.rustOrders.textContent = this.formatInt(rust.ordersPerSec || 0);
      this.nodes.rustLatency.textContent = `${this.formatInt(rust.latency || 0)}us`;
      this.nodes.rustMemory.textContent = `${this.formatInt(rust.memory || 0)}MB`;
    }
    if (cpp) {
      this.nodes.cppOrders.textContent = this.formatInt(cpp.ordersPerSec || 0);
      this.nodes.cppLatency.textContent = `${this.formatInt(cpp.latency || 0)}us`;
      this.nodes.cppMemory.textContent = `${this.formatInt(cpp.memory || 0)}MB`;
    }

    if (rust && cpp && rust.ordersPerSec > 0 && cpp.ordersPerSec > 0) {
      const rustLeads = rust.ordersPerSec >= cpp.ordersPerSec;
      const fast = rustLeads ? rust.ordersPerSec : cpp.ordersPerSec;
      const slow = rustLeads ? cpp.ordersPerSec : rust.ordersPerSec;
      const delta = Math.round(((fast - slow) / slow) * 100);
      this.nodes.winner.textContent = rustLeads ? 'RUST' : 'CXX';
      this.nodes.speedDifference.textContent = `${delta}%`;
    }

    this.nodes.priceSamples.textContent = this.formatInt(this.priceHistory.count);
  }

  renderDepth() {
    const bids = this.state.bids;

    this.writeBookRows(this.askRows, this.state.asks, false, true);
    this.writeBookRows(this.bidRows, bids, true, false);

    if (bids.length > 0 && this.state.asks.length > 0) {
      const bestBid = Number(bids[0].price || 0);
      const bestAsk = Number(this.state.asks[0].price || 0);
      const spread = Math.max(0, bestAsk - bestBid);
      this.nodes.spread.textContent = `SPREAD ${this.formatPrice(spread)}`;
    } else {
      this.nodes.spread.textContent = 'SPREAD 0.00';
    }
  }

  writeBookRows(rows, levels, isBid, reverse) {
    const limit = Math.min(levels.length, this.depthLimit);
    for (let i = 0; i < rows.length; i += 1) {
      const row = rows[i];
      const levelIndex = reverse ? limit - i - 1 : i;
      const level = levelIndex >= 0 ? levels[levelIndex] : null;
      if (!level) {
        row.cells[0].textContent = '';
        row.cells[1].textContent = '';
        row.cells[2].textContent = '';
        row.row.style.visibility = 'hidden';
        continue;
      }

      const price = Number(level.price || 0);
      const quantity = Number(level.quantity || level.total_quantity || 0);
      const notional = price * quantity;
      row.row.style.visibility = 'visible';
      row.cells[0].textContent = this.formatPrice(price);
      row.cells[1].textContent = this.formatInt(quantity);
      row.cells[2].textContent = this.formatCompact(notional);
      row.cells[0].className = 'price';
      row.cells[1].className = 'size';
      row.cells[2].className = 'notional';
      row.row.classList.toggle('bid-row', isBid);
    }
  }

  renderTrades() {
    for (let i = 0; i < this.tradeRows.length; i += 1) {
      const row = this.tradeRows[i];
      const trade = this.state.trades[i];
      if (!trade) {
        row.cells[0].textContent = '';
        row.cells[1].textContent = '';
        row.cells[2].textContent = '';
        row.row.style.visibility = 'hidden';
        continue;
      }

      const price = Number(trade.price || 0);
      const previousPrice = Number(this.state.trades[i + 1]?.price || this.lastTradePrice || price);
      const className =
        price > previousPrice
          ? 'trade-price trade-up'
          : price < previousPrice
            ? 'trade-price trade-down'
            : 'trade-price trade-flat';
      if (i === 0 && price > 0) {
        this.lastTradePrice = price;
      }

      row.row.style.visibility = 'visible';
      row.cells[0].textContent = this.formatTime(trade.timestamp);
      row.cells[1].className = className;
      row.cells[1].textContent = this.formatPrice(price);
      row.cells[2].textContent = this.formatInt(trade.quantity || 0);
    }
  }

  startThroughputClock() {
    window.setInterval(() => {
      const now = performance.now();
      const elapsed = (now - this.lastThroughputTime) / 1000;
      const totalOrders = Number(this.state.metrics.totalOrders || 0);
      const diff = totalOrders - this.lastRenderedOrderCount;
      const throughput = elapsed > 0 ? Math.max(0, Math.round(diff / elapsed)) : 0;
      this.nodes.throughput.textContent = this.formatInt(throughput);
      this.lastRenderedOrderCount = totalOrders;
      this.lastThroughputTime = now;
    }, 1000);
  }

  drawPrice() {
    this.drawSeries(this.nodes.priceCanvas, [{ series: this.priceHistory, color: '#d8d8d8' }]);
  }

  drawPerformance() {
    this.drawSeries(this.nodes.perfCanvas, [
      { series: this.perfHistory.rustOps, color: '#c28b5f' },
      { series: this.perfHistory.cppOps, color: '#81a9d8' }
    ]);
  }

  drawSeries(canvas, series) {
    const context = canvas.getContext('2d', { alpha: false });
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.floor(rect.width));
    const height = Math.max(1, Math.floor(rect.height));

    if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
      canvas.width = width * dpr;
      canvas.height = height * dpr;
    }

    context.setTransform(dpr, 0, 0, dpr, 0, 0);
    context.fillStyle = '#060606';
    context.fillRect(0, 0, width, height);

    context.strokeStyle = '#151515';
    context.lineWidth = 1;
    for (let x = 0; x < width; x += 48) {
      context.beginPath();
      context.moveTo(x, 0);
      context.lineTo(x, height);
      context.stroke();
    }
    for (let y = 0; y < height; y += 32) {
      context.beginPath();
      context.moveTo(0, y);
      context.lineTo(width, y);
      context.stroke();
    }

    let min = Number.POSITIVE_INFINITY;
    let max = Number.NEGATIVE_INFINITY;
    for (const line of series) {
      for (let i = 0; i < line.series.count; i += 1) {
        const value = this.readSeries(line.series, i);
        if (value > 0) {
          min = Math.min(min, value);
          max = Math.max(max, value);
        }
      }
    }

    if (!Number.isFinite(min) || !Number.isFinite(max) || min === max) {
      return;
    }

    const pad = (max - min) * 0.08;
    min -= pad;
    max += pad;

    for (const line of series) {
      if (line.series.count < 2) {
        continue;
      }

      context.strokeStyle = line.color;
      context.lineWidth = 1;
      context.beginPath();
      for (let index = 0; index < line.series.count; index += 1) {
        const value = this.readSeries(line.series, index);
        const x = (index / (line.series.limit - 1)) * width;
        const y = height - ((value - min) / (max - min)) * height;
        if (index === 0) {
          context.moveTo(x, y);
        } else {
          context.lineTo(x, y);
        }
      }
      context.stroke();
    }
  }

  formatInt(value) {
    return Math.round(Number(value) || 0).toLocaleString('en-US');
  }

  formatCompact(value) {
    const numeric = Number(value) || 0;
    if (numeric >= 1_000_000) {
      return `${(numeric / 1_000_000).toFixed(2)}M`;
    }
    if (numeric >= 1_000) {
      return `${(numeric / 1_000).toFixed(1)}K`;
    }
    return numeric.toFixed(0);
  }

  formatPrice(value) {
    return (Number(value) || 0).toFixed(2);
  }

  formatTime(timestamp) {
    const date = new Date(Number(timestamp) || Date.now());
    return date.toLocaleTimeString('en-US', {
      hour12: false,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });
  }
}

document.addEventListener('DOMContentLoaded', () => {
  new TerminalDashboard();
});
