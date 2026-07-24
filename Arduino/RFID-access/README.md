# HACKTOR OS — RFID Access Control

Firmware for **Arduino** (or compatible microcontrollers) + **RC522 (MFRC522)** RFID reader and a **piezo buzzer**, designed with a cyberpunk terminal aesthetic. It checks scanned RFID cards against a predefined target UID and triggers distinct audio-visual alarms for authorized or unauthorized access.

## Features

* **UID Verification** — scans incoming RFID cards and compares their unique ID against a hardcoded target UID (`19 95 07 A4`)
* **Audio Feedback** — plays custom multi-note melodies via a buzzer for both access granted and access denied events
* **Serial Terminal Log** — outputs detailed status logs and hex-formatted UIDs to the Serial Monitor (9600 baud)

> Use only on cards you own or have permission to use for testing, prototyping, or personal security projects.

## Pinout — Arduino ↔ RC522 & Buzzer

Power the RC522 module with **3.3V only** — 5V can damage it.

| Component pin | Arduino Pin | Note |
| --- | --- | --- |
| RC522 SDA (SS) | Pin 10 | Chip select |
| RC522 SCK | Pin 13 | SPI clock (standard Arduino SPI) |
| RC522 MOSI | Pin 11 | SPI data (Arduino → RC522) |
| RC522 MISO | Pin 12 | SPI data (RC522 → Arduino) |
| RC522 IRQ | Unconnected | Unused |
| RC522 GND | GND | Ground |
| RC522 RST | Pin 9 | Reset |
| RC522 3.3V | 3.3V | **NEVER 5V** |
| Buzzer (+) | Pin 8 | Piezo buzzer positive pin |
| Buzzer (-) | GND | Ground |

## Project Structure

```
hacktor-os/
├── hacktor-os.ino      # Main firmware (RFID reader logic, sound generator)
└── pitches.h           # Header file defining musical note frequencies

```

## Requirements & Dependencies

1. **Libraries**:
* `MFRC522` library by GithubCommunity (install via Arduino IDE Library Manager)
* `SPI` library (built-in)


2. **Files**:
* Ensure `pitches.h` is placed in the same directory as your sketch so the buzzer melodies compile correctly.



## Setup and Upload

1. Wire the RC522 reader and the piezo buzzer to your Arduino according to the pinout table above.
2. Open `hacktor-os.ino` in the Arduino IDE.
3. Configure your target UID in the code if you want to use a different card:
```cpp
byte targetUID[4] = {0x19, 0x95, 0x07, 0xA4};

```


4. Select your board and port, then click **Upload**.
5. Open the **Serial Monitor** at **9600 baud** to see the system boot sequence and real-time scan logs.
