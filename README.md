# Acidgotchi 🐾

**A Tamagotchi that eats WiFi.** A tiny virtual pet — a kawaii cat — living on a sub-1-inch screen, powered by an **ESP32-C6**. It doesn't need food pellets: it survives by *sniffing the radio spectrum around you*. Every WiFi network, Bluetooth device, and (later) Zigbee signal in the air is a snack. Feed it a strong, fresh signal and it purrs and grows; leave it in a dead zone and it gets hungry, bored, and needy.

Part of the **[Acid Zero](https://github.com/chetansaini53)** toolkit — the playful, friendly cousin of the pwnagotchi.

> ⚠️ **Status: WIP.** Concept + design spec committed. Firmware in progress (pinning the exact display driver first).

[![Platform](https://img.shields.io/badge/platform-ESP32--C6-blue)](#hardware)
[![UI](https://img.shields.io/badge/UI-LVGL%20kawaii-ff69b4)](#tech)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

---

## The idea

Acidgotchi is a **real virtual pet with a real appetite for RF**. On a timer it scans the nearby spectrum and *eats* what it finds:

- 📶 **Strong signal = good food.** RSSI between **-30 and -50 dBm** → the pet is thrilled, does a `nom nom nom` animation, and grows faster.
- 🆕 **New discovery = a treat.** A never-seen-before **SSID** or **BLE MAC address** is a bonus snack — extra happiness.
- 😐 **Weak signal = a small nibble.** Far-away/faint signals barely fill its belly.
- 🥱 **Same old signals = boredom.** Eating the exact same networks for more than a week → it gets bored and stops enjoying its meals.

And because it's a needy little thing, it has a **personality**: hunger alerts, "play with me!", "take me for a walk 🚶", "oi oi, where are you owner?", attention-seeking, a bit annoying, always playful.

## Feeding & mood

| Signal | Pet reaction |
|---|---|
| RSSI -30…-50 dBm (strong) | 😻 very happy, fast growth, `nom nom nom` |
| RSSI -50…-70 dBm (medium) | 🙂 content nibble |
| RSSI < -80 dBm (weak) | 😾 unsatisfied, still hungry |
| **new** SSID / BLE MAC | ✨ discovery bonus + happy dance |
| same signals > 1 week | 🥱 bored, "feed me something new!" |
| no signals / dead zone | 😿 hungry → sad → needy alerts |

## Evolution

The pet grows up based on how many unique signals it has eaten over its life:

**🥚 Baby  →  🐱 Teenager  →  🥷 Pro Hacker Pet**

Each stage changes its face, size, and attitude (the Pro Hacker Pet is smug about how much spectrum it has devoured).

## Hardware

| Part | Detail |
|---|---|
| **MCU** | ESP32-**C6** (RISC-V, 160 MHz, 8 MB flash) |
| **Radios** | WiFi 6 (2.4 GHz) · Bluetooth 5 (LE) · IEEE **802.15.4** (Zigbee/Thread) |
| **Display** | on-board sub-1-inch color LCD *(exact controller being confirmed)* |
| **Power** | USB-C |

The C6's three radios are what make the pet's diet varied — it can taste WiFi, Bluetooth, and Zigbee, all from one chip.

## Tech

- **LVGL** for the animated kawaii cat faces and mood transitions (HD, smooth).
- **LovyanGFX** as the display flush driver.
- Non-blocking scan scheduler so the pet stays animated while it sniffs.
- Persistent pet state (age, evolution, seen-signals memory) in flash.

## Roadmap

- [ ] Confirm display controller + bring up LVGL on the C6
- [ ] Kawaii cat sprite set (idle / nom-nom / happy / hungry / bored / sleep)
- [ ] WiFi + BLE scan → feeding engine + RSSI moods
- [ ] Seen-signals memory + new-discovery bonus + boredom timer
- [ ] Evolution stages (Baby → Teenager → Pro Hacker Pet)
- [ ] Needy personality: alerts, "play with me", attention-seeking messages
- [ ] Zigbee / 802.15.4 energy-scan as "exotic food" (phase 2)
- [ ] Save/restore pet across reboots

## License & author

MIT © 2026 **Chetan Saini** — part of the **Acid Zero** toolkit.

Built with [LVGL](https://lvgl.io) + [LovyanGFX](https://github.com/lovyan03/LovyanGFX).
