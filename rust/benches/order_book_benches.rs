use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion};
use lock_free_order_book::order::{Order, Side};
use lock_free_order_book::order_book::OrderBook;
use rand::prelude::*;

fn setup_book_with_orders(n: u32) -> (OrderBook, Vec<Order>) {
    let mut rng = StdRng::seed_from_u64(42);
    let mut book = OrderBook::new();
    let orders: Vec<Order> = (0..n)
        .map(|i| {
            let side = if rng.gen::<bool>() {
                Side::Buy
            } else {
                Side::Sell
            };
            let price = rng.gen_range(90..110);
            let quantity = rng.gen_range(1..100);
            Order::new(i as u64, side, price, quantity)
        })
        .collect();

    for order in &orders {
        book.add_order(black_box(*order));
    }

    (book, orders)
}

fn benchmark_add_order(c: &mut Criterion) {
    c.bench_function("add_10k_orders", |b| {
        b.iter(|| {
            let mut book = OrderBook::new();
            for i in 0..10_000 {
                book.add_order(black_box(Order::new(i, Side::Buy, 100, 10)));
            }
        })
    });
}

fn benchmark_cancel_order(c: &mut Criterion) {
    let (mut book, orders) = setup_book_with_orders(10_000);
    let mut rng = StdRng::seed_from_u64(42);

    c.bench_function("cancel_1k_orders", |b| {
        b.iter(|| {
            for _ in 0..1_000 {
                let order_to_cancel = orders.choose(&mut rng).unwrap();
                book.cancel_order(
                    black_box(order_to_cancel.order_id),
                    black_box(order_to_cancel.side),
                    black_box(order_to_cancel.price),
                );
            }
        })
    });
}

fn benchmark_modify_order(c: &mut Criterion) {
    let (mut book, orders) = setup_book_with_orders(10_000);
    let mut rng = StdRng::seed_from_u64(42);

    c.bench_function("modify_1k_orders", |b| {
        b.iter(|| {
            for _ in 0..1_000 {
                let order_to_modify = orders.choose(&mut rng).unwrap();
                let new_quantity = rng.gen_range(1..200);
                book.modify_order(
                    black_box(order_to_modify.order_id),
                    black_box(order_to_modify.side),
                    black_box(order_to_modify.price),
                    black_box(new_quantity),
                );
            }
        })
    });
}

fn benchmark_matching_engine(c: &mut Criterion) {
    let mut group = c.benchmark_group("matching_engine");
    for size in [1_000, 10_000, 100_000].iter() {
        group.bench_with_input(BenchmarkId::from_parameter(size), size, |b, &size| {
            b.iter_with_setup(
                || {
                    let mut book = OrderBook::new();
                    // Pre-fill the book with ask orders
                    for i in 0..size {
                        book.add_order(Order::new(i, Side::Sell, 100 + (i % 10), 10));
                    }
                    book
                },
                |mut book| {
                    // Send a sweeping buy order to match
                    book.add_order(black_box(Order::new(size, Side::Buy, 110, size * 10)));
                },
            );
        });
    }
    group.finish();
}

criterion_group!(
    benches,
    benchmark_add_order,
    benchmark_cancel_order,
    benchmark_modify_order,
    benchmark_matching_engine
);
criterion_main!(benches);
