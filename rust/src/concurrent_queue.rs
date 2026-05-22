use crossbeam::queue::ArrayQueue;
use std::sync::Arc;

use crate::order::Order;

#[derive(Clone)]
pub struct OrderQueue {
    inner: Arc<ArrayQueue<Order>>,
}

impl OrderQueue {
    pub fn new(capacity: usize) -> Self {
        OrderQueue {
            inner: Arc::new(ArrayQueue::new(capacity)),
        }
    }

    pub fn push(&self, order: Order) -> Result<(), Order> {
        self.inner.push(order)
    }

    pub fn pop(&self) -> Option<Order> {
        self.inner.pop()
    }
}

#[cfg(test)]
mod tests {
    use super::OrderQueue;
    use crate::order::{Order, Side};
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn spsc_queue_basic() {
        let q = OrderQueue::new(16);
        let producer = q.clone();
        let consumer = q.clone();
        let handle = thread::spawn(move || {
            for i in 0..8 {
                let order = Order::new(i, Side::Buy, 100, 1);
                assert!(producer.push(order).is_ok());
            }
        });
        handle.join().unwrap();

        for i in 0..8 {
            let o = consumer.pop().expect("Order should be available");
            assert_eq!(o.order_id, i);
        }
        assert!(consumer.pop().is_none());
    }

    #[test]
    fn mpsc_queue_concurrent() {
        let producers = 4;
        let orders_per = 1000;
        // Use capacity equal to total orders to avoid blocking
        let q = Arc::new(OrderQueue::new(producers * orders_per));
        let mut handles = Vec::new();
        for t in 0..producers {
            let producer = q.clone();
            handles.push(thread::spawn(move || {
                for i in 0..orders_per {
                    let id = (t * orders_per + i) as u64;
                    let order = Order::new(id, Side::Sell, 100, 1);
                    while producer.push(order).is_err() {}
                }
            }));
        }
        for h in handles {
            h.join().unwrap();
        }
        // Drain
        let mut count = 0;
        while let Some(_o) = q.pop() {
            count += 1;
        }
        assert_eq!(count, 4 * 1000);
    }

    #[test]
    fn order_book_concurrent_processing() {
        use crate::order::Side;
        use crate::order_book::OrderBook;
        let producers = 4;
        let orders_per = 1000;
        let queue = Arc::new(OrderQueue::new(producers * orders_per));
        let mut handles = Vec::new();
        for t in 0..producers {
            let q = queue.clone();
            handles.push(thread::spawn(move || {
                for i in 0..orders_per {
                    let order = Order::new((t * orders_per + i) as u64, Side::Buy, 100, 1);
                    while q.push(order).is_err() {}
                }
            }));
        }
        for h in handles {
            h.join().unwrap();
        }
        let mut book = OrderBook::new();
        let mut count = 0;
        while let Some(order) = queue.pop() {
            book.add_order(order);
            count += 1;
        }
        assert_eq!(count, producers * orders_per);
    }
}
