use criterion::{criterion_group, criterion_main, Criterion};
use lock_free_order_book::concurrent_queue::OrderQueue;
use lock_free_order_book::order::{Order, Side};
use lock_free_order_book::order_book::OrderBook;
use std::sync::{Arc, Mutex};
use std::thread;

fn bench_concurrent_order_book(c: &mut Criterion) {
    let producers = 4;
    let orders_per = 10_000;
    let total = producers * orders_per;

    c.bench_function("concurrent_order_book_4x10k", move |b| {
        b.iter(|| {
            // Shared queue and book
            let queue = Arc::new(OrderQueue::new(total));
            let book = Arc::new(Mutex::new(OrderBook::new()));
            let mut handles = Vec::new();
            for t in 0..producers {
                let q = queue.clone();
                handles.push(thread::spawn(move || {
                    for i in 0..orders_per {
                        let id = (t * orders_per + i) as u64;
                        let order = Order::new(id, Side::Buy, 100, 1);
                        // spin until pushed
                        while q.push(order).is_err() {}
                    }
                }));
            }

            let q_consumer = queue.clone();
            let book_consumer = book.clone();
            let consumer = thread::spawn(move || {
                let mut count = 0;
                while count < total {
                    if let Some(order) = q_consumer.pop() {
                        book_consumer.lock().unwrap().add_order(order);
                        count += 1;
                    } else {
                        thread::yield_now();
                    }
                }
            });

            for h in handles {
                h.join().unwrap();
            }
            consumer.join().unwrap();
        });
    });
}

criterion_group!(concurrent_order_book_benches, bench_concurrent_order_book);
criterion_main!(concurrent_order_book_benches);
