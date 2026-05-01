const io = require('socket.io-client');

const NUM_CLIENTS = 100;
const DURATION_SEC = 5;

let clients = [];
let updatesReceived = 0;

console.log(`Starting stress test: Connecting ${NUM_CLIENTS} WebSocket clients for ${DURATION_SEC} seconds...`);

for (let i = 0; i < NUM_CLIENTS; i++) {
  const socket = io('http://localhost:3001', { transports: ['websocket'] });
  
  socket.on('orderbook-update', (data) => {
    updatesReceived++;
  });

  socket.on('connect_error', (err) => {
    console.error('Connection error:', err);
  });

  clients.push(socket);
}

setTimeout(() => {
  clients.forEach(c => c.disconnect());
  console.log(`Stress test complete! Received ${updatesReceived} total orderbook updates across ${NUM_CLIENTS} clients in ${DURATION_SEC} seconds.`);
  console.log(`Average updates per second: ${updatesReceived / DURATION_SEC}`);
  process.exit(0);
}, DURATION_SEC * 1000);
