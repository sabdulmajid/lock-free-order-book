use crate::order::{Order, Side};
use crate::order_book::OrderBook;
use rand::prelude::*;
use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PriceLevelData {
    pub price: f64,
    pub quantity: u64,
    pub order_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OrderBookSnapshot {
    pub bids: Vec<PriceLevelData>,
    pub asks: Vec<PriceLevelData>,
    pub trades: Vec<TradeData>,
    pub metrics: MetricsData,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TradeData {
    pub id: u64,
    pub price: f64,
    pub quantity: u64,
    pub timestamp: u64,
    #[serde(rename = "buyOrderId")]
    pub buy_order_id: u64,
    #[serde(rename = "sellOrderId")]
    pub sell_order_id: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MetricsData {
    #[serde(rename = "totalOrders")]
    pub total_orders: u64,
    #[serde(rename = "totalTrades")]
    pub total_trades: u64,
    pub volume: u64,
    #[serde(rename = "lastPrice")]
    pub last_price: f64,
}

pub struct MarketSimulator {
    order_book: OrderBook,
    order_id_counter: u64,
    current_price: f64,
    recent_trades: Vec<TradeData>,
    metrics: MetricsData,
    rng: StdRng,
}

impl MarketSimulator {
    pub fn new() -> Self {
        let mut simulator = MarketSimulator {
            order_book: OrderBook::new(),
            order_id_counter: 1,
            current_price: 100.0,
            recent_trades: Vec::new(),
            metrics: MetricsData {
                total_orders: 0,
                total_trades: 0,
                volume: 0,
                last_price: 100.0,
            },
            rng: StdRng::seed_from_u64(42),
        };

        // Initialize with some orders
        simulator.initialize_market();
        simulator
    }

    fn initialize_market(&mut self) {
        // Add initial orders around the current price
        for _ in 0..50 {
            let order = self.generate_order();
            self.add_order_to_book(order);
        }
    }

    fn generate_order(&mut self) -> Order {
        let side = if self.rng.gen::<bool>() { Side::Buy } else { Side::Sell };
        let spread = 0.5;
        let price_variation = (self.rng.gen::<f64>() - 0.5) * 2.0; // -1 to 1
        
        let price = match side {
            Side::Buy => self.current_price - spread / 2.0 + price_variation,
            Side::Sell => self.current_price + spread / 2.0 + price_variation,
        };
        
        let price_cents = (price * 100.0).round() as u64;
        let quantity = self.rng.gen_range(10..=100);
        
        Order::new(self.order_id_counter, side, price_cents, quantity)
    }

    fn add_order_to_book(&mut self, order: Order) {
        self.order_id_counter += 1;
        self.metrics.total_orders += 1;
        
        // For simulation, we'll add the order directly
        // In a real system, this would go through the matching engine
        self.order_book.add_order(order);
    }

    pub fn simulate_market_activity(&mut self) -> OrderBookSnapshot {
        // Add some new orders
        for _ in 0..self.rng.gen_range(1..=5) {
            let order = self.generate_order();
            self.add_order_to_book(order);
        }

        // Occasionally generate a trade by crossing the spread
        if self.rng.gen::<f64>() < 0.3 {
            self.generate_trade();
        }

        self.get_snapshot()
    }

    fn generate_trade(&mut self) {
        let trade_price = self.current_price + (self.rng.gen::<f64>() - 0.5) * 0.2;
        let trade_quantity = self.rng.gen_range(10..=50);
        
        let trade = TradeData {
            id: self.order_id_counter,
            price: trade_price,
            quantity: trade_quantity,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_millis() as u64,
            buy_order_id: self.rng.gen_range(1..self.order_id_counter),
            sell_order_id: self.rng.gen_range(1..self.order_id_counter),
        };

        self.recent_trades.push(trade);
        if self.recent_trades.len() > 100 {
            self.recent_trades.remove(0);
        }

        self.metrics.total_trades += 1;
        self.metrics.volume += trade_quantity;
        self.metrics.last_price = trade_price;
        self.current_price = trade_price;
    }

    pub fn get_snapshot(&mut self) -> OrderBookSnapshot {
        // For now, we'll generate mock order book data
        // In a real implementation, this would extract from the actual order book
        let mut bids = Vec::new();
        let mut asks = Vec::new();

        // Generate mock bid levels
        for i in 0..20 {
            let price = self.current_price - 0.5 - (i as f64 * 0.01);
            let quantity = self.rng.gen_range(50..=500);
            bids.push(PriceLevelData {
                price,
                quantity,
                order_count: self.rng.gen_range(1..=5),
            });
        }

        // Generate mock ask levels
        for i in 0..20 {
            let price = self.current_price + 0.5 + (i as f64 * 0.01);
            let quantity = self.rng.gen_range(50..=500);
            asks.push(PriceLevelData {
                price,
                quantity,
                order_count: self.rng.gen_range(1..=5),
            });
        }

        OrderBookSnapshot {
            bids,
            asks,
            trades: self.recent_trades.iter().rev().take(10).cloned().collect(),
            metrics: self.metrics.clone(),
        }
    }
}