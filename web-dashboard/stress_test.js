const { io } = require('socket.io-client');

const URL = process.env.STRESS_URL || 'http://localhost:3001';
const NUM_CLIENTS = Number(process.env.STRESS_CLIENTS || 8);
const DURATION_SEC = Number(process.env.STRESS_DURATION_SEC || 6);
const MIN_UPDATES_PER_CLIENT_SEC = Number(process.env.STRESS_MIN_UPDATES_PER_CLIENT_SEC || 100);
const STRICT = process.env.STRESS_STRICT === '1';

const clients = [];
const perClientUpdates = new Array(NUM_CLIENTS).fill(0);
let snapshotsReceived = 0;
let malformedPayloads = 0;
let missingEngineMetrics = 0;
let engineMismatches = 0;

function validLevels(levels) {
  return (
    Array.isArray(levels) &&
    levels.every(
      (level) =>
        Number.isFinite(Number(level.price)) &&
        Number.isFinite(Number(level.quantity)) &&
        Number.isFinite(Number(level.order_count ?? level.orderCount ?? 0))
    )
  );
}

function inspectPayload(data) {
  if (!data || !validLevels(data.bids) || !validLevels(data.asks) || !data.metrics || !data.stockInfo) {
    malformedPayloads++;
    return;
  }

  const rust = data.metrics.rustPerformance;
  const cpp = data.metrics.cppPerformance;
  if (!rust || !cpp || !Number.isFinite(Number(rust.ordersPerSec)) || !Number.isFinite(Number(cpp.ordersPerSec))) {
    missingEngineMetrics++;
    return;
  }

  engineMismatches += Number(rust.mismatches || 0) + Number(cpp.mismatches || 0);
}

async function main() {
  console.log(`Connecting ${NUM_CLIENTS} clients to ${URL} for ${DURATION_SEC}s`);

  for (let i = 0; i < NUM_CLIENTS; i += 1) {
    const socket = io(URL, { transports: ['websocket'], reconnection: false, timeout: 2000 });

    socket.on('orderbook-snapshot', (data) => {
      snapshotsReceived++;
      inspectPayload(data);
    });

    socket.on('orderbook-update', (data) => {
      perClientUpdates[i] += 1;
      inspectPayload(data);
    });

    socket.on('connect_error', (error) => {
      console.error(`client ${i} connection error: ${error.message}`);
      malformedPayloads++;
    });

    clients.push(socket);
  }

  await new Promise((resolve) => setTimeout(resolve, DURATION_SEC * 1000));
  for (const socket of clients) {
    socket.disconnect();
  }

  const totalUpdates = perClientUpdates.reduce((sum, value) => sum + value, 0);
  const updatesPerClientSec = totalUpdates / NUM_CLIENTS / DURATION_SEC;
  const minClientUpdates = Math.min(...perClientUpdates);
  const maxClientUpdates = Math.max(...perClientUpdates);

  console.log(
    JSON.stringify(
      {
        totalUpdates,
        updatesPerClientSec,
        minClientUpdates,
        maxClientUpdates,
        snapshotsReceived,
        malformedPayloads,
        missingEngineMetrics,
        engineMismatches
      },
      null,
      2
    )
  );

  const failed =
    malformedPayloads > 0 ||
    missingEngineMetrics > 0 ||
    engineMismatches > 0 ||
    (STRICT && updatesPerClientSec < MIN_UPDATES_PER_CLIENT_SEC);

  if (failed) {
    process.exit(1);
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
