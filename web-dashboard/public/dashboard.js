class OrderBookDashboard {
    constructor() {
        this.socket = io();
        this.priceHistory = [];
        this.maxPriceHistory = 50;
        this.chart = null;
        this.lastUpdateTime = Date.now();
        this.orderCount = 0;
        this.currentLanguage = 'rust';
        
        this.initializeChart();
        this.setupSocketListeners();
        this.setupEventListeners();
        this.startPerformanceTracking();
    }

    setupEventListeners() {
        // Stock selector
        const stockSelector = document.getElementById('stock-selector');
        stockSelector.addEventListener('change', (e) => {
            this.socket.emit('switch-stock', e.target.value);
        });

        // Language toggle
        const rustBtn = document.getElementById('rust-btn');
        const cppBtn = document.getElementById('cpp-btn');
        
        rustBtn.addEventListener('click', () => {
            this.switchLanguage('rust');
        });
        
        cppBtn.addEventListener('click', () => {
            this.switchLanguage('cpp');
        });
    }

    switchLanguage(lang) {
        this.currentLanguage = lang;
        
        // Update button states
        document.getElementById('rust-btn').classList.toggle('active', lang === 'rust');
        document.getElementById('cpp-btn').classList.toggle('active', lang === 'cpp');
        
        // You could emit this to server to switch backend implementation
        console.log(`Switched to ${lang} implementation`);
    }

    initializeChart() {
        const ctx = document.getElementById('price-chart').getContext('2d');
        this.chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Price',
                    data: [],
                    borderColor: '#4CAF50',
                    backgroundColor: 'rgba(76, 175, 80, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    }
                },
                scales: {
                    x: {
                        display: false,
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)'
                        }
                    },
                    y: {
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)'
                        },
                        ticks: {
                            color: '#ffffff'
                        }
                    }
                },
                elements: {
                    point: {
                        radius: 0
                    }
                },
                animation: {
                    duration: 0
                }
            }
        });
    }

    setupSocketListeners() {
        this.socket.on('connect', () => {
            console.log('Connected to server');
            document.getElementById('connection-status').className = 'status-dot connected';
        });

        this.socket.on('disconnect', () => {
            console.log('Disconnected from server');
            document.getElementById('connection-status').className = 'status-dot disconnected';
        });

        this.socket.on('orderbook-snapshot', (data) => {
            this.updateOrderBook(data);
            this.updateMetrics(data.metrics);
            this.updateStockInfo(data.stockInfo);
        });

        this.socket.on('orderbook-update', (data) => {
            this.updateOrderBook(data);
            this.updateMetrics(data.metrics);
            this.updateStockInfo(data.stockInfo);
        });

        this.socket.on('new-trades', (trades) => {
            this.updateTrades(trades);
        });
    }

    updateOrderBook(data) {
        this.renderOrderBookSide('bids', data.bids, true);
        this.renderOrderBookSide('asks', data.asks, false);
        this.updateSpread(data.bids, data.asks);
        this.updatePriceChart(data.metrics.lastPrice);
    }

    renderOrderBookSide(side, orders, isBid) {
        const container = document.getElementById(`${side}-list`);
        container.innerHTML = '';

        orders.forEach(order => {
            const row = document.createElement('div');
            row.className = `order-row ${side.slice(0, -1)}`;
            
            const price = order.price.toFixed(2);
            const quantity = order.quantity.toLocaleString();
            const total = (order.price * order.quantity).toLocaleString();

            if (isBid) {
                row.innerHTML = `
                    <span>${total}</span>
                    <span>${quantity}</span>
                    <span>$${price}</span>
                `;
            } else {
                row.innerHTML = `
                    <span>$${price}</span>
                    <span>${quantity}</span>
                    <span>${total}</span>
                `;
            }

            container.appendChild(row);
        });
    }

    updateSpread(bids, asks) {
        if (bids.length > 0 && asks.length > 0) {
            const bestBid = bids[0].price;
            const bestAsk = asks[0].price;
            const spread = bestAsk - bestBid;
            document.getElementById('spread').textContent = `Spread: $${spread.toFixed(2)}`;
        }
    }

    updatePriceChart(price) {
        const now = new Date();
        this.priceHistory.push({
            time: now.toLocaleTimeString(),
            price: price
        });

        if (this.priceHistory.length > this.maxPriceHistory) {
            this.priceHistory.shift();
        }

        this.chart.data.labels = this.priceHistory.map(p => p.time);
        this.chart.data.datasets[0].data = this.priceHistory.map(p => p.price);
        this.chart.update('none');
    }

    updateMetrics(metrics) {
        document.getElementById('total-orders').textContent = metrics.totalOrders.toLocaleString();
        document.getElementById('total-trades').textContent = metrics.totalTrades.toLocaleString();
        document.getElementById('volume').textContent = metrics.volume.toLocaleString();
        
        // Update performance comparison
        if (metrics.rustPerformance) {
            document.getElementById('rust-orders-sec').textContent = metrics.rustPerformance.ordersPerSec.toLocaleString();
            document.getElementById('rust-latency').textContent = `${metrics.rustPerformance.latency}μs`;
            document.getElementById('rust-memory').textContent = `${metrics.rustPerformance.memory}MB`;
        }
        
        if (metrics.cppPerformance) {
            document.getElementById('cpp-orders-sec').textContent = metrics.cppPerformance.ordersPerSec.toLocaleString();
            document.getElementById('cpp-latency').textContent = `${metrics.cppPerformance.latency}μs`;
            document.getElementById('cpp-memory').textContent = `${metrics.cppPerformance.memory}MB`;
        }
        
        // Update winner and speed difference
        if (metrics.rustPerformance && metrics.cppPerformance) {
            const rustFaster = metrics.rustPerformance.ordersPerSec > metrics.cppPerformance.ordersPerSec;
            const winner = rustFaster ? 'Rust' : 'C++';
            const fasterOps = rustFaster ? metrics.rustPerformance.ordersPerSec : metrics.cppPerformance.ordersPerSec;
            const slowerOps = rustFaster ? metrics.cppPerformance.ordersPerSec : metrics.rustPerformance.ordersPerSec;
            const speedDiff = Math.round(((fasterOps - slowerOps) / slowerOps) * 100);
            
            document.getElementById('performance-winner').textContent = winner;
            document.getElementById('speed-difference').textContent = `+${speedDiff}% faster`;
        }
        
        this.orderCount = metrics.totalOrders;
    }

    updateStockInfo(stockInfo) {
        if (stockInfo) {
            document.getElementById('current-price').textContent = `$${stockInfo.price.toFixed(2)}`;
            
            // Update stock selector to match
            const selector = document.getElementById('stock-selector');
            selector.value = stockInfo.symbol;
        }
    }

    updateTrades(trades) {
        const container = document.getElementById('trades-list');
        
        // Use a Set to avoid duplicate trades if we're receiving overlapping snapshots
        trades.forEach(trade => {
            const tradeId = `${trade.timestamp}-${trade.price}-${trade.quantity}`;
            if (document.getElementById(tradeId)) return;

            const row = document.createElement('div');
            row.id = tradeId;
            row.className = 'trade-row';
            
            const time = new Date(trade.timestamp).toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
            const price = trade.price.toFixed(2);
            const quantity = trade.quantity.toLocaleString();

            // Determine color based on side (buy/sell)
            // In a real T&S tape, color depends on whether the trade hit the bid or lifted the ask
            const isUptick = Math.random() > 0.5; 
            const colorClass = isUptick ? 'trade-up' : 'trade-down';

            row.innerHTML = `
                <span class="trade-time">${time}</span>
                <span class="trade-price ${colorClass}">$${price}</span>
                <span class="trade-size">${quantity}</span>
            `;

            container.insertBefore(row, container.firstChild);
        });

        // Keep only last 15 trades for clarity and performance
        while (container.children.length > 15) {
            container.removeChild(container.lastChild);
        }
    }

    startPerformanceTracking() {
        let lastOrderCount = 0;
        
        setInterval(() => {
            const currentTime = Date.now();
            const timeDiff = (currentTime - this.lastUpdateTime) / 1000;
            const orderDiff = this.orderCount - lastOrderCount;
            const throughput = Math.round(orderDiff / timeDiff);
            
            document.getElementById('throughput').textContent = throughput.toLocaleString();
            
            lastOrderCount = this.orderCount;
            this.lastUpdateTime = currentTime;
        }, 1000);
    }
}

// Initialize dashboard when page loads
document.addEventListener('DOMContentLoaded', () => {
    new OrderBookDashboard();
});