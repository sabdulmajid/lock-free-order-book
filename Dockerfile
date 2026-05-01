FROM ubuntu:22.04

# Prevent interactive prompts during apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies for C++, Rust, and Node.js
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    curl \
    git \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install Node.js (v20)
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y nodejs

# Install Rust
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

WORKDIR /app

# Copy the entire project
COPY . .

# Build C++ project
WORKDIR /app/cpp
RUN mkdir -p build && cd build && cmake .. && make order_book_cpp

# Build Rust project
WORKDIR /app/rust
RUN cargo build --release --bin order_book_rust

# Setup Node.js dashboard
WORKDIR /app/web-dashboard
RUN npm install

# Expose port and start
ENV PORT=3000
EXPOSE 3000
CMD ["npm", "start"]
