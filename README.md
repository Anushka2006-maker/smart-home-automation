# Smart Home Automation System with Cloud Monitoring

An IoT smart home system built on the ESP32. Three sensors drive three relay-controlled appliances automatically, every reading and state change is synced to Firebase Realtime Database, and a web dashboard shows the system live and lets you take manual control from anywhere.

The whole system runs in simulation (Wokwi), so no physical hardware is needed to reproduce it.

- **Live dashboard:** `https://<your-username>.github.io/smart-home-automation/`
- **Wokwi simulation:** `<paste your Wokwi share link here>`

![Dashboard screenshot]
<img width="1912" height="911" alt="image" src="https://github.com/user-attachments/assets/25ae668c-769b-4aae-9f29-fd7fcc3ce4ce" />


## Background

This project upgrades an earlier build: a single LED switched through a relay from the Blynk IoT app. That version only offered manual on/off control through a third-party platform. This version adds sensor-driven automation, multiple appliances, and a self-built cloud pipeline and dashboard.

| Original project | This version |
|---|---|
| Manual on/off from the Blynk app | Automatic control from sensor readings, with manual override |
| One LED on one relay | Three appliances on three relays (fan, light, auxiliary) |
| Third-party no-code platform | Custom Firebase pipeline and web dashboard |
| No history | Timestamped activity log of every state change |

## Features

- **Automatic control:** the fan, light and auxiliary appliance respond to temperature, light level, motion and humidity.
- **Hysteresis on thresholds:** the fan and auxiliary appliance use separate on and off thresholds, so relays do not chatter around a single value.
- **Manual override:** an Auto / Manual switch on the dashboard hands control to the user. Switching to Manual starts from the current appliance states, so nothing flips unexpectedly.
- **Cloud sync:** sensor values, appliance states, mode and uptime are uploaded to Firebase every 5 seconds.
- **Remote commands:** the ESP32 checks Firebase for dashboard commands every 3 seconds.
- **Activity log:** each appliance change is recorded with a server timestamp, the reason, and whether it was automatic or manual.
- **Offline tolerance:** log entries are queued on the device and sent when WiFi is available.
- **Device status:** the dashboard shows whether the ESP32 is online, with uptime or last-seen time.

## System architecture

```
 Sensors                    ESP32                      Cloud                 Browser
┌──────────┐          ┌───────────────┐          ┌────────────────┐     ┌──────────────┐
│ DHT22    │──────────▶               │  HTTPS   │   Firebase     │     │  Dashboard   │
│ LDR      │──────────▶  Automation   │  PATCH   │   Realtime     │ GET │ (GitHub      │
│ PIR      │──────────▶  logic        │─────────▶│   Database     │◀────│  Pages)      │
└──────────┘          │               │          │                │     │              │
                      │  3 relays     │◀─────────│  /control      │◀────│  PATCH       │
┌──────────┐          │               │   GET    │                │     │  (override)  │
│ Fan      │◀─────────│               │          └────────────────┘     └──────────────┘
│ Light    │◀─────────│               │
│ Aux      │◀─────────└───────────────┘
└──────────┘
```

**Data flow**
1. The ESP32 reads its sensors and applies the automation rules (or follows dashboard commands in Manual mode).
2. Every 5 seconds it uploads sensors, appliance states and status to Firebase with one `PATCH` request.
3. Every 3 seconds it reads `/smarthome/control` and applies the dashboard's mode and switch values.
4. The dashboard polls Firebase every 2 seconds, renders the live state, and writes commands back to `/smarthome/control`.

## Tech stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (Arduino framework, C++) |
| Sensors and actuators | DHT22, photoresistor (LDR) module, PIR motion sensor, 3 relay modules |
| Simulation | Wokwi |
| Networking | WiFi, HTTPS REST calls with `HTTPClient` and `WiFiClientSecure` |
| Cloud | Firebase Realtime Database (REST API) |
| Dashboard | HTML, CSS and JavaScript (no frameworks), hosted on GitHub Pages |
| Libraries | Adafruit DHT sensor library, Adafruit Unified Sensor |

## Hardware (simulated)

| Component | ESP32 pin |
|---|---|
| DHT22 data | GPIO 4 |
| LDR module analog out | GPIO 34 |
| PIR motion output | GPIO 27 |
| Relay 1 (fan) | GPIO 25 |
| Relay 2 (light) | GPIO 26 |
| Relay 3 (auxiliary) | GPIO 33 |

All sensors and relay modules are powered from the 3V3 rail and share ground.

## Automation rules

| Appliance | Turns ON when | Turns OFF when |
|---|---|---|
| Fan | Temperature rises above 30 °C | Temperature falls below 28 °C |
| Light | It is dark and motion was detected in the last 10 seconds | No recent motion, or the room is bright |
| Auxiliary | Humidity rises above 70 % | Humidity falls below 65 % |

Thresholds are constants at the top of `firmware/sketch.ino`. The LDR threshold depends on the simulated module, so calibrate it from the raw value printed in the Serial Monitor.

## Firebase data structure

```
/smarthome
  /sensors      temperature, humidity, ldr, dark, motion
  /appliances   fan, light, aux                      (reported by the ESP32)
  /control      mode ("auto" | "manual"), fan, light, aux   (written by the dashboard)
  /status       mode, uptime, updated                (server timestamp)
  /logs         <id>: appliance, state, reason, mode, ts
```

## Repository structure

```
smart-home-automation/
├── index.html            Dashboard (served by GitHub Pages)
├── README.md
├── docs/                 Screenshots
└── firmware/
    ├── sketch.ino        ESP32 firmware
    ├── diagram.json      Wokwi circuit definition
    └── libraries.txt     Arduino libraries used
```

## Getting started

### 1. Run the simulation
1. Go to [wokwi.com](https://wokwi.com) and create a new ESP32 project.
2. Replace the contents of `sketch.ino` and `diagram.json` with the files in `firmware/`.
3. Add the libraries listed in `firmware/libraries.txt`.
4. Press Start. The ESP32 joins Wokwi's built-in `Wokwi-GUEST` network.

### 2. Set up Firebase
1. Create a project in the [Firebase console]([https://console.firebase.google.com](https://smart-home-automation-a51e2-default-rtdb.asia-southeast1.firebasedatabase.app/) and add a **Realtime Database**.
2. For development, set the rules to allow read and write (see the security note below).
3. Copy the database URL and set it as `DB_URL` in `firmware/sketch.ino` and `DB` in `index.html`.

### 3. Open the dashboard
Open the GitHub Pages link, or open `index.html` locally. With the simulation running, the status pill turns green and readings appear within a few seconds.

### 4. Try it out
- Set the DHT22 temperature above 30 °C: the fan relay turns on and a log entry appears.
- Lower the LDR light level and trigger the PIR sensor: the light relay turns on.
- Set humidity above 70 %: the auxiliary relay turns on.
- On the dashboard, choose **Manual** and flip a switch: the matching relay follows within about 3 seconds.

## Limitations

- **Open database rules.** The development rules allow anyone with the URL to read and write, so anyone who finds the dashboard link can control the appliances. This is acceptable for a simulation but not for real use.
- **Polling, not push.** The device and dashboard poll over HTTPS. Commands can take a few seconds to take effect, and each request opens a new TLS connection.
- **Insecure TLS client.** The firmware calls `setInsecure()`, which skips certificate validation. This is convenient in simulation but should not ship in a real device.
- **Simulated sensors.** Light levels depend on how Wokwi models the LDR module, so thresholds need calibration against any real sensor.

## Future improvements

- Firebase Authentication with per-user rules, and certificate validation on the device.
- Real-time updates using MQTT or Firebase streaming instead of polling.
- Scheduling and scenes, such as a night mode that switches several appliances together.
- Energy monitoring with a current sensor to track appliance usage over time.
- Voice assistant integration and push notifications.
- Deployment on physical hardware with mains-rated relays and proper isolation.

## Author

Anushka Dubey, B.Tech Electronics and Communication Engineering, VIT Bhopal University.
