const express = require('express');
const path = require('path');

const PORT = process.env.PORT || 3000;
const API_KEY = process.env.API_KEY || 'change-me';   // must match the ESP32 firmware
const OFFLINE_AFTER_MS = 15000;                        // no reading for this long = offline
const MAX_HISTORY = 300;                               // ~10 min at one reading per 2 s
const MAX_ALERTS = 50;

const VALID_STATUS = new Set(['SAFE', 'WARNING', 'FLOOD']);

const state = {
  device: null,
  level: 0,
  raw: 0,
  status: 'UNKNOWN',
  warn: 35,
  flood: 65,
  rssi: null,
  lastSeen: null,
  online: false,
};
const history = [];   // { t, level }
const alerts = [];    // newest first
const clients = new Set();

function broadcast(event, data) {
  const msg = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const res of clients) res.write(msg);
}

function addAlert(severity, message) {
  const alert = { id: `${Date.now()}-${Math.random().toString(36).slice(2, 6)}`, t: Date.now(), severity, message };
  alerts.unshift(alert);
  if (alerts.length > MAX_ALERTS) alerts.pop();
  console.log(`[alert:${severity}] ${message}`);
  broadcast('alert', alert);
}

// Set ALLOWED_ORIGIN on Render to your Netlify URL, e.g. https://my-flood.netlify.app
const ALLOWED_ORIGIN = process.env.ALLOWED_ORIGIN || '*';

const app = express();
app.use((req, res, next) => {
  res.set('Access-Control-Allow-Origin', ALLOWED_ORIGIN);
  res.set('Access-Control-Allow-Headers', 'Content-Type, x-api-key');
  res.set('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));  // optional: backend can still serve the dashboard

app.get('/health', (req, res) => res.send('ok'));
app.get('/', (req, res) => res.send('Flood Sense 360 backend is running. Open the dashboard on Netlify. Check /health and /api/state.'));

// ESP32 posts here
app.post('/api/reading', (req, res) => {
  if (req.get('x-api-key') !== API_KEY) {
    console.log('Rejected reading: API key does not match the one set on Render.');
    return res.status(401).json({ error: 'bad api key' });
  }

  const { device, level, raw, status, warn, flood, rssi } = req.body || {};
  if (typeof level !== 'number' || level < 0 || level > 100 || !VALID_STATUS.has(status)) {
    return res.status(400).json({ error: 'invalid payload' });
  }

  const previousStatus = state.status;
  const wasOnline = state.online;
  const now = Date.now();

  Object.assign(state, {
    device: device || 'unknown',
    level,
    raw: raw ?? 0,
    status,
    warn: warn ?? state.warn,
    flood: flood ?? state.flood,
    rssi: rssi ?? null,
    lastSeen: now,
    online: true,
  });

  history.push({ t: now, level });
  if (history.length > MAX_HISTORY) history.shift();

  if (!wasOnline && previousStatus !== 'UNKNOWN') addAlert('info', 'Sensor is back online.');

  if (status !== previousStatus) {
    if (status === 'FLOOD') addAlert('critical', `Flood alert. Water level reached ${level}%.`);
    else if (status === 'WARNING' && previousStatus === 'FLOOD') addAlert('warning', `Water is falling but still high (${level}%).`);
    else if (status === 'WARNING') addAlert('warning', `Water is rising. Level is ${level}%.`);
    else if (status === 'SAFE' && previousStatus !== 'UNKNOWN') addAlert('info', `Water level is back to normal (${level}%).`);
  }

  console.log(`Reading from ${state.device}: ${level}% (${status})`);
  broadcast('reading', { state, point: { t: now, level } });
  res.json({ ok: true });
});

// Dashboard subscribes here for live updates
app.get('/api/stream', (req, res) => {
  res.set({
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive',
    'X-Accel-Buffering': 'no',
  });
  res.flushHeaders();
  res.write(`event: init\ndata: ${JSON.stringify({ state, history, alerts })}\n\n`);
  clients.add(res);

  const heartbeat = setInterval(() => res.write(': ping\n\n'), 25000);
  req.on('close', () => {
    clearInterval(heartbeat);
    clients.delete(res);
  });
});

// Plain JSON snapshot (handy for debugging)
app.get('/api/state', (req, res) => res.json({ state, history, alerts }));

// Mark the sensor offline when readings stop arriving
setInterval(() => {
  if (state.online && Date.now() - state.lastSeen > OFFLINE_AFTER_MS) {
    state.online = false;
    addAlert('warning', 'Sensor went offline. No readings received.');
    broadcast('state', state);
  }
}, 5000);

app.listen(PORT, '0.0.0.0', () => {
  console.log(`Flood dashboard running on http://localhost:${PORT}`);
});
