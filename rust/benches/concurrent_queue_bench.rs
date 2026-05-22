use criterion::{criterion_group, criterion_main, Criterion};
use std::sync::Arc;
use std::thread;

use lock_free_order_book::concurrent_queue::OrderQueue;
use lock_free_order_book::order::{Order, Side};

fn bench_spsc_queue(c: &mut Criterion) {
    let total = 100_000;
    c.bench_function("spsc_queue_100k", |b| {
        b.iter(|| {
            let queue = OrderQueue::new(total);
            let producer = queue.clone();
            let handle = thread::spawn(move || {
                for i in 0..total {
                    let order = Order::new(i as u64, Side::Buy, 100, 1);
                    producer.push(order).expect("push failed");
                }
            });
            let mut count = 0;
            while count < total {
                if queue.pop().is_some() {
                    count += 1;
                }
            }
            handle.join().unwrap();
        })
    });
}

fn bench_mpsc_queue(c: &mut Criterion) {
    let producers = 4;
    let per = 50_000;
    c.bench_function("mpsc_queue_4x50k", |b| {
        b.iter(|| {
            let queue = Arc::new(OrderQueue::new(producers * per));
            let mut handles = Vec::new();
            for t in 0..producers {
                let q = queue.clone();
                handles.push(thread::spawn(move || {
                    for i in 0..per {
                        let id = (t * per + i) as u64;
                        let order = Order::new(id, Side::Sell, 100, 1);
                        while q.push(order).is_err() {}
                    }
                }));
            }
            let mut count = 0;
            while count < producers * per {
                if queue.pop().is_some() {
                    count += 1;
                }
            }
            for h in handles {
                h.join().unwrap();
            }
        })
    });
}

criterion_group!(concurrent_queue_benches, bench_spsc_queue, bench_mpsc_queue);
criterion_main!(concurrent_queue_benches);
