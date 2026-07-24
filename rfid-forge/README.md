# RFID-FORGE — Universal RFID Tool for ESP32

Firmware for the **NodeMCU ESP-32S** + **RC522 (MFRC522)** reader, controlled via a web interface. The ESP32 creates its own WiFi hotspot and mDNS name, so you can connect to it directly with your phone/PC — no internet or home network required.

## Features

* **Read** — reads the UID + the contents of all sectors when a card is tapped (tries several of the most common/default factory keys)
* **Data translation** — HEX / DEC / ASCII / BIN toggle for each read block
* **Write block** — manually write 16 bytes (32 hex characters) to a selected block
* **Clone** — saves a dump from a source card and writes it to a target card; optionally includes the UID (Block 0) if the target card is a "magic"/Gen1A type (special rewritable cards sold specifically for this purpose)
* **Format** — resets the card to factory keys/access bits and wipes the data

> Use only on cards you own or have permission to use (e.g., backing up your own access card, testing your own system).

## Pinout — ESP32 (NodeMCU-32S) ↔ RC522

The RC522 runs on **VSPI** (ESP32 hardware SPI pins). Power the RC522 module with **3.3V only** — 5V can damage it.

| RC522 pin | ESP32 GPIO | Note |
| --- | --- | --- |
| SDA (SS) | GPIO 5 | chip select |
| SCK | GPIO 18 | SPI clock |
| MOSI | GPIO 23 | SPI data (ESP32 → RC522) |
| MISO | GPIO 19 | SPI data (RC522 → ESP32) |
| IRQ | unconnected | unused |
| GND | GND |  |
| RST | GPIO 22 | reset |
| 3.3V | 3.3V | **NEVER 5V** |

Optional:

| Feature | ESP32 GPIO | Note |
| --- | --- | --- |
| Status LED | GPIO 2 | most devboards have a built-in LED here |

## Project Structure

```
rfid-forge/
├── platformio.ini
├── src/
│   └── main.cpp        # firmware (WiFi AP, mDNS, RFID logic, WebSocket API)
└── data/
    └── index.html       # web page (uploaded separately to LittleFS)

```

## Build and Upload (PlatformIO)

1. Open the `rfid-forge` folder in PlatformIO (VS Code extension or CLI).
2. Wire the RC522 according to the table above and connect the ESP32 via USB.
3. Upload the firmware:
```
pio run -t upload

```


4. **Upload the web page to LittleFS** (this is a separate step, otherwise the page won't be available):
```
pio run -t uploadfs

```


In the PlatformIO IDE, this is the "Upload Filesystem Image" button.
5. Open the serial monitor (`pio device monitor`) — you will see the IP address and confirmation that mDNS is running.

## Connection

* WiFi network: **`RFID-FORGE`**, password **`clone1234`** (you can change this in `main.cpp` under the `AP_SSID` / `AP_PASSWORD` variables)
* Open in your browser: **`[http://rfid.local](http://rfid.local)`**
* If mDNS doesn't work on your browser/OS (sometimes an issue on Android), use the IP directly: **`[http://192.168.4.1](http://192.168.4.1)`**

## How it Works Internally

The browser communicates with the ESP32 via WebSocket (`/ws`) using a simple JSON protocol:

**Browser → ESP32**

```json
{"cmd":"read"}
{"cmd":"write_block","block":4,"hex":"00112233445566778899AABBCCDDEEFF"}
{"cmd":"clone_write","overwrite_uid":false}
{"cmd":"format"}
{"cmd":"cancel"}

```

**ESP32 → Browser**

```json
{"event":"status","msg":"..."}
{"event":"dump","uid":"...","type":"...","blocks":[{"idx":0,"sector":0,"trailer":false,"auth":true,"hex":"..."}]}
{"event":"progress","block":12,"total":64}
{"event":"done","action":"read|write|clone|format","ok":true}
{"event":"error","msg":"..."}

```

The firmware waits in `loop()` for a card to be tapped only when a specific mode is set (read/write/clone/format) — otherwise, the RC522 sits idle.

## Limitations of this Version

* Targets **MIFARE Classic 1K** (the most common — blue/white key fobs, most simple attendance/access cards). For 4K cards, only the first 16 sectors (64 blocks) are processed. MIFARE Ultralight / NTAG (different memory structure) are not supported.
* UID rewriting (Block 0) works **only** on special "magic"/Gen1A cards designed by the manufacturer for UID rewriting — the hardware will not allow this on standard secured cards (this is by design).
* Authentication only tries a small set of the most common/factory default keys; cards with truly custom (unknown) keys will not be read.
