#include "order.h"

std::ostream& operator<<(std::ostream& os, const Order& order) {
    os << "Order ID: " << order.order_id << ", Side: " << (order.side == Side::Buy ? "Buy" : "Sell")
       << ", Price: " << order.price << ", Quantity: " << order.quantity << ", Timestamp: " << order.timestamp;
    return os;
}
