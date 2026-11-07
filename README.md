# Acidgotchi 🐾

**A Tamagotchi that eats WiFi.** A tiny virtual pet — a kawaii cat — living on a sub-1-inch screen, powered by an **ESP32-C6**. It doesn't need food pellets: it survives by *sniffing the radio spectrum around you*. Every WiFi network and Bluetooth device in the air is a snack. Feed it a strong, fresh signal and it purrs and grows; leave it in a dead zone and it gets hungry, bored, and needy.

Part of the **[Acid Zero](https://github.com/chetansaini53)** toolkit — the playful, friendly cousin of the pwnagotchi.

[![Platform](https://img.shields.io/badge/platform-ESP32--C6-blue)](#hardware)
[![Display](https://img.shields.io/badge/display-ST7789%20172x320-purple)](#hardware)
[![GFX](https://img.shields.io/badge/graphics-Arduino__GFX-ff69b4)](#how-it-is-built)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

> ✅ **Status: working.** Firmware runs on the Waveshare ESP32-C6-LCD-1.47: animated emotion sprites, live WiFi + BLE feeding, mood engine, day counter, and NVS persistence. Clone → install deps → flash → it's alive.

---

## The idea

Acidgotchi is a **real virtual pet with a real appetite for RF**. On a timer it scans the nearby spectrum and *eats* what it finds:

- 📶 **Strong signal = good food.** A strong RSSI → the pet is thrilled, does an eating animation, and grows faster.
- 🆕 **New discovery = a treat.** A never-seen-before **SSID** or **BLE MAC address** is a bonus snack — extra happiness.
- 😐 **Weak signal = a small nibble.** Far-away/faint signals barely fill its belly.
- 🥱 **Same old signals = boredom.** Eating the exact same networks over and over → it gets bored and stops enjoying its meals.

And because it's a needy little thing, it has a **personality**: hunger alerts, "take me for a walk", "oi, where are you owner?", attention-seeking one-liners — always playful.

## Feeding & mood

| Signal | Pet reaction |
|---|---|
| Strong RSSI (close AP/device) | 😻 very happy, fast growth, eating animation |
| Medium RSSI | 🙂 content nibble |
| Weak RSSI | 😾 unsatisfied, still hungry |
| **new** SSID / BLE MAC | ✨ discovery bonus + happy |
| same signals, repeatedly | 🥱 bored, "feed me something new!" |
| no signals / dead zone | 😿 hungry → sad → needy alerts |
| low energy | 🥱 naps, then wakes and resumes |

The scan interval **grows as the pet ages** (5 s on day 1, +10 s per day) so a mature pet sips power instead of scanning constantly.

## Evolution

Grows up based on how many **unique** signals it has eaten over its life:

**🥚 Baby → 🐱 Teen → 🥷 Pro**

## Hardware

| Part | Detail |
|---|---|
| **Board** | Waveshare **ESP32-C6-LCD-1.47** |
| **MCU** | ESP32-**C6** (RISC-V, 160 MHz) |
| **Radios** | WiFi 6 (2.4 GHz) · Bluetooth 5 (LE) · 802.15.4 (Zigbee/Thread, unused for now) |
| **Display** | 1.47" **ST7789** IPS, 172×320, portrait |
| **Power** | USB-C (native USB-Serial/JTAG) |

Pins used (baked into the sketch): SPI `SCK=7, MOSI=6`, `DC=15, CS=14, RST=21`, backlight `GPIO22`, ST7789 x-offset `34`.

## How it is built

- **[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)** (`moononournation`) for the display + sprite blitting — **not** LVGL/LovyanGFX.
- **Non-blocking async WiFi scan** (`scanNetworks(true,true)` + `scanComplete()` polling) so the animation never freezes while it sniffs.
- **BLE** via the ESP32 core's bundled Bluedroid stack.
- Emotion animations are **pre-baked RGB565 frame arrays** in [`acidgotchi/emotions.h`](acidgotchi/emotions.h) (the C6 has no room for a runtime GIF decoder). Source art lives in [`acidgotchi/assets/`](acidgotchi/assets) and is converted by [`tools/gen_emotions.py`](tools/gen_emotions.py).
- Pet state (age, evolution, seen-signals memory, WiFi/BLE eat counters) persists across reboots in **NVS** (`Preferences`).
- Radio is powered down between scans (`WIFI_OFF`) to run cool on battery.

## Build & flash

**1. Install the ESP32 core** (board manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`), then the graphics library.

Arduino IDE: *Boards Manager* → **esp32** by Espressif · *Library Manager* → **GFX Library for Arduino**.
Select board **ESP32C6 Dev Module**, set **Partition Scheme = Huge APP**, **USB CDC On Boot = Enabled**, then Upload.

**Or with `arduino-cli`:**

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "GFX Library for Arduino"
arduino-cli compile --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app,CDCOnBoot=cdc acidgotchi
arduino-cli upload  --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app,CDCOnBoot=cdc -p COM15 acidgotchi
```

> ⚠️ **`CDCOnBoot=cdc` is required** — without it `Serial` goes to UART0 instead of the USB port and the port won't enumerate for upload/monitoring. Replace `COM15` with your port.
> 💡 `PartitionScheme=huge_app` is needed because the baked sprite frames make the binary large.

## Sprites — re-skin the pet

The five faces are plain animated GIFs in [`acidgotchi/assets/`](acidgotchi/assets): `happy`, `needatt`, `oidekho`, `sad`, `eating`.

To use your own art, drop in GIFs with the **same five names** and regenerate the header:

```bash
pip install pillow
python tools/gen_emotions.py
```

It coalesces each GIF, samples 8 frames, fits them into a 132×110 box (aspect-preserved), converts to RGB565, and rewrites `acidgotchi/emotions.h`. No code change needed — reflash and the new faces are live.

## Roadmap

- [x] Bring up ST7789 on the C6 with Arduino_GFX
- [x] Emotion sprite set (happy / eating / sad / needy / attention)
- [x] WiFi + BLE scan → feeding engine + RSSI moods
- [x] Seen-signals memory + new-discovery bonus + boredom
- [x] Needy personality: rotating mood one-liners
- [x] Save/restore pet across reboots (NVS)
- [x] Non-blocking async scan so animation stays smooth
- [ ] Evolution art per stage (Baby → Teen → Pro)
- [ ] Battery + BMS and on-device charging
- [ ] Zigbee / 802.15.4 energy-scan as "exotic food"

## Credits

- Firmware, mechanics & concept: **Chetan Saini** — part of the **Acid Zero** toolkit.
- Graphics: **[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)** (MIT).
- Emotion sprite GIFs in [`acidgotchi/assets/`](acidgotchi/assets) are by their original creators. **Source & license:** _add attribution link here before relying on these specific assets — swap them with your own via `tools/gen_emotions.py` to make the pet fully yours._

## License & author

MIT © 2026 **Chetan Saini** — the MIT license covers the firmware and tooling in this repo. The sprite GIFs under `acidgotchi/assets/` keep their own original licenses (see **Credits**).
