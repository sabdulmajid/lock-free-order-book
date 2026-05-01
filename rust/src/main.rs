use lock_free_order_book::order::{Order, Side};
use lock_free_order_book::order_book::OrderBook;
use serde_json::Value;
use std::io::{self, BufRead, Write};
use std::time::Instant;

fn main() {
    let stdin = io::stdin();
    let mut stdout = io::stdout();
    let mut book = OrderBook::new();

    for line in stdin.lock().lines() {
        let line = match line {
            Ok(l) => l,
            Err(_) => break,
        };

        if line.trim().is_empty() {
            continue;
        }

        match serde_json::from_str::<Value>(&line) {
            Ok(req) => {
                let action = req["action"].as_str().unwrap_or("");

                if action == "batch" {
                    let start = Instant::now();
                    let mut processed = 0;

                    if let Some(orders) = req["orders"].as_array() {
                        for o in orders {
                            let id = o["id"].as_u64().unwrap_or(0);
                            let side_str = o["side"].as_str().unwrap_or("buy");
                            let side = if side_str == "buy" { Side::Buy } else { Side::Sell };
                            let price_f64 = o["price"].as_f64().unwrap_or(0.0);
                            let price = (price_f64 * 100.0).round() as u64;
                            let quantity = o["quantity"].as_u64().unwrap_or(0);

                            let order = Order::new(id, side, price, quantity);
                            book.add_order(order);
                            processed += 1;
                        }
                    }

                    let latency = start.elapsed().as_nanos() as u64;

                    let resp = serde_json::json!({
                        "type": "batch_result",
                        "latency_ns": latency,
                        "processed": processed
                    });

                    writeln!(stdout, "{}", resp.to_string()).unwrap();
                    stdout.flush().unwrap();
                } else if action == "snapshot" {
                    let snapshot = book.get_snapshot();
                    let resp = serde_json::json!({
                        "type": "snapshot",
                        "bids": snapshot.bids,
                        "asks": snapshot.asks
                    });

                    writeln!(stdout, "{}", resp.to_string()).unwrap();
                    stdout.flush().unwrap();
                }
            }
            Err(e) => {
                let resp = serde_json::json!({
                    "type": "error",
                    "message": e.to_string()
                });
                writeln!(stdout, "{}", resp.to_string()).unwrap();
                stdout.flush().unwrap();
            }
        }
    }
}