# ESP32 Flood Monitor

```
flood-monitor/
  firmware/flood_monitor/flood_monitor.ino   <- upload to ESP32 (Arduino IDE)
  backend/server.js                          <- Node.js server (receives data, live stream, alerts)
  backend/public/index.html                  <- dashboard (served by the backend)
```

## Wiring (breadboard)

| Part | Pin on part | Connect to |
|---|---|---|
| Water level sensor | + | ESP32 **3V3** (not 5V, the ADC pin is 3.3 V max) |
| | - | GND |
| | S | **GPIO34** |
| RGB LED (common cathode) | Red leg | 220 ohm -> **GPIO25** |
| | Green leg | 220 ohm -> **GPIO26** |
| | Blue leg | 220 ohm -> **GPIO27** |
| | Longest leg | GND |
| 5V active buzzer | + | **VIN** (5V from USB) |
| | - | NPN transistor collector (2N2222 or BC547) |
| Transistor | Emitter | GND |
| | Base | 1k ohm -> **GPIO23** |

Why the transistor: GPIO pins output 3.3 V and only a few mA, which is too weak for a 5V buzzer. If you have a common anode LED, set `COMMON_ANODE = true` in the firmware and connect the common leg to 3V3.

## 1. Deploy the backend on Render

1. Push the `backend` folder to a GitHub repo (or the whole project, and set Root Directory to `backend`).
2. Render -> New -> Web Service -> pick the repo.
   - Build command: `npm install`
   - Start command: `npm start`
3. Environment variables:
   - `API_KEY` = a long random secret
   - `ALLOWED_ORIGIN` = your Netlify URL, e.g. `https://my-flood.netlify.app` (add after step 2)
4. Deploy, then test `https://YOUR-APP-NAME.onrender.com/health` (should say `ok`).

## 2. Deploy the dashboard on Netlify

1. In `backend/public/index.html`, set `API_BASE` to your Render URL (no trailing slash).
2. Netlify -> Add new site -> deploy manually, and drag in the `public` folder (or connect the repo with publish directory `backend/public`).
3. Copy the Netlify URL into Render's `ALLOWED_ORIGIN` and redeploy.

## 3. Flash the ESP32

In `flood_monitor.ino` set `WIFI_SSID`, `WIFI_PASS`, `SERVER_URL` (`https://YOUR-APP-NAME.onrender.com/api/reading`) and `API_KEY` (same as on Render). Upload from Arduino IDE ("esp32 by Espressif Systems" board package).

Render's free tier sleeps after about 15 minutes without traffic and takes up to a minute to wake. The ESP32 posting every 5 seconds keeps it awake once running, but the first reading after a restart can be slow. A paid instance removes this.

## 4. Calibrate

Open Serial Monitor at 115200 baud. Note `raw` with the sensor dry and with it submerged to your maximum mark. Put those into `RAW_DRY` and `RAW_WET`, adjust `WARN_PCT` / `FLOOD_PCT`, and re-upload. The dashboard picks up the thresholds automatically.

## Behaviour

| Status | LED | Buzzer | Default level |
|---|---|---|---|
| SAFE | Green | Off | below 35% |
| WARNING | Yellow | Short beep every 2 s | 35% and up |
| FLOOD | Blinking red | Fast beeping | 65% and up |

The LED and buzzer work without Wi-Fi. The dashboard shows "Sensor offline" if no reading arrives for 15 seconds. Readings are sent every 5 seconds from a background task, so slow network calls never delay the buzzer.

## Notes
- Water level sensors with exposed traces corrode if left submerged. Power them only when needed for long-term installs, or use a waterproof ultrasonic sensor.
- Data is kept in memory, so it resets when the server restarts. Swap the `history` and `alerts` arrays for a database if you need long-term logs.
- Use a strong `API_KEY`. The firmware skips TLS certificate checking (setInsecure), which is fine for a hobby project; pin the Render root certificate for stricter security.
