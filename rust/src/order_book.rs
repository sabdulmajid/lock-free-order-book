use std::collections::{BTreeMap, VecDeque};
use crate::order::{Order, Side};
use crate::trade::Trade;

pub struct PriceLevel {
    pub total_quantity: u64,
    pub orders: VecDeque<Order>,
}

impl PriceLevel {
    pub fn new() -> Self {
        PriceLevel {
            total_quantity: 0,
            orders: VecDeque::new(),
        }
    }
}

pub struct OrderBook {
    pub bids: BTreeMap<u64, PriceLevel>,
    pub asks: BTreeMap<u64, PriceLevel>,
}

impl OrderBook {
    pub fn new() -> Self {
        OrderBook {
            bids: BTreeMap::new(),
            asks: BTreeMap::new(),
        }
    }

    fn match_order(&mut self, order: &mut Order) -> Vec<Trade> {
        let mut trades = Vec::new();
        
        match order.side {
            Side::Buy => {
                let mut prices_to_remove = Vec::new();
                for (price, level) in self.asks.iter_mut() {
                    if order.price < *price || order.quantity == 0 {
                        break;
                    }
                    
                    while let Some(mut maker_order) = level.orders.pop_front() {
                        if order.quantity == 0 {
                            level.orders.push_front(maker_order);
                            break;
                        }
                        
                        let trade_qty = std::cmp::min(order.quantity, maker_order.quantity);
                        trades.push(Trade::new(order.order_id, maker_order.order_id, trade_qty, *price));
                        
                        order.quantity -= trade_qty;
                        maker_order.quantity -= trade_qty;
                        level.total_quantity -= trade_qty;
                        
                        if maker_order.quantity > 0 {
                            level.orders.push_front(maker_order);
                        }
                    }
                    
                    if level.orders.is_empty() {
                        prices_to_remove.push(*price);
                    }
                }
                
                for price in prices_to_remove {
                    self.asks.remove(&price);
                }
            },
            Side::Sell => {
                let mut prices_to_remove = Vec::new();
                // We need to iterate bids in reverse (highest first)
                for (price, level) in self.bids.iter_mut().rev() {
                    if order.price > *price || order.quantity == 0 {
                        break;
                    }
                    
                    while let Some(mut maker_order) = level.orders.pop_front() {
                        if order.quantity == 0 {
                            level.orders.push_front(maker_order);
                            break;
                        }
                        
                        let trade_qty = std::cmp::min(order.quantity, maker_order.quantity);
                        trades.push(Trade::new(order.order_id, maker_order.order_id, trade_qty, *price));
                        
                        order.quantity -= trade_qty;
                        maker_order.quantity -= trade_qty;
                        level.total_quantity -= trade_qty;
                        
                        if maker_order.quantity > 0 {
                            level.orders.push_front(maker_order);
                        }
                    }
                    
                    if level.orders.is_empty() {
                        prices_to_remove.push(*price);
                    }
                }
                
                for price in prices_to_remove {
                    self.bids.remove(&price);
                }
            }
        }
        
        trades
    }

    pub fn add_order(&mut self, mut order: Order) -> Vec<Trade> {
        order.timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos() as u64;

        let trades = self.match_order(&mut order);

        if order.quantity > 0 {
            let (book_side, price) = match order.side {
                Side::Buy => (&mut self.bids, order.price),
                Side::Sell => (&mut self.asks, order.price),
            };

            let price_level = book_side.entry(price).or_insert_with(PriceLevel::new);
            price_level.total_quantity += order.quantity;
            price_level.orders.push_back(order);
        }

        trades
    }

    pub fn cancel_order(&mut self, order_id: u64, side: Side, price: u64) -> bool {
        let book_side = match side {
            Side::Buy => &mut self.bids,
            Side::Sell => &mut self.asks,
        };

        if let Some(price_level) = book_side.get_mut(&price) {
            if let Some(index) = price_level.orders.iter().position(|o| o.order_id == order_id) {
                let order = price_level.orders.remove(index).unwrap();
                price_level.total_quantity -= order.quantity;
                if price_level.orders.is_empty() {
                    book_side.remove(&price);
                }
                return true;
            }
        }
        false
    }

    pub fn modify_order(&mut self, order_id: u64, side: Side, price: u64, new_quantity: u64) -> bool {
        let book_side = match side {
            Side::Buy => &mut self.bids,
            Side::Sell => &mut self.asks,
        };

        if let Some(price_level) = book_side.get_mut(&price) {
            for order in &mut price_level.orders {
                if order.order_id == order_id {
                    price_level.total_quantity -= order.quantity;
                    order.quantity = new_quantity;
                    price_level.total_quantity += new_quantity;
                    return true;
                }
            }
        }
        false
    }

    pub fn get_snapshot(&self) -> crate::market_simulator::OrderBookSnapshot {
        let mut bids = Vec::new();
        for (price, level) in self.bids.iter().rev().take(20) {
            bids.push(crate::market_simulator::PriceLevelData {
                price: *price as f64 / 100.0,
                quantity: level.total_quantity,
                order_count: level.orders.len(),
            });
        }

        let mut asks = Vec::new();
        for (price, level) in self.asks.iter().take(20) {
            asks.push(crate::market_simulator::PriceLevelData {
                price: *price as f64 / 100.0,
                quantity: level.total_quantity,
                order_count: level.orders.len(),
            });
        }

        crate::market_simulator::OrderBookSnapshot {
            bids,
            asks,
            trades: Vec::new(),
            metrics: crate::market_simulator::MetricsData {
                total_orders: 0,
                total_trades: 0,
                volume: 0,
                last_price: 0.0,
            }
        }
    }
}