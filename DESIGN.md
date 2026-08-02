# Acidgotchi — Design Spec

Implementation blueprint. This is the source of truth for the firmware once the display is confirmed.

## 1. Core loop

```
every SCAN_INTERVAL (e.g. 20-30 s):
    scan WiFi (scanNetworks)         -> list of {ssid, bssid, rssi}
    scan BLE  (active scan ~3 s)     -> list of {mac, name, rssi}
    (phase 2) 802.15.4 energy scan   -> per-channel energy
    feed(pet, results)
between scans:
    animate current mood/face at ~30 fps (LVGL)
    tick needs (hunger, boredom, attention) on their own timers
    handle any user interaction (button/touch)
```

Scanning must never freeze the animation — run scans on a schedule and keep the LVGL tick alive (single-core C6: yield frequently, keep scan windows short).

## 2. Feeding engine

For each signal seen in a scan:

```
strength_score = map(rssi, -90..-30, 0..100)          // clamp
is_new         = bssid/mac NOT in seen[]               // discovery
food = base_food(strength_score)
if is_new:  food += DISCOVERY_BONUS ; play(nom_nom) ; happiness += NEW_JOY
if seen recently & often (stale): food *= BORED_FACTOR // diminishing returns
hunger  -= food
belly   += food
lifetime_unique += is_new ? 1 : 0
remember(bssid/mac, now)
```

- `seen[]` = ring/LRU of recent BSSIDs/MACs with last-seen timestamps (bounded, e.g. 256 entries).
- **Boredom:** if the *set* of signals hasn't changed meaningfully for > 7 days (or > N scans with 0 new), mood → BORED, and food from repeats is heavily discounted.
- **Dead zone:** 0 signals for K scans → hunger climbs fast → HUNGRY → SAD → needy alerts.

## 3. Stats (0-100)

| Stat | Rises when | Falls when |
|---|---|---|
| **Hunger** | time passes, no food | eating (esp. strong/new signals) |
| **Happiness** | new discoveries, strong signals, play | boredom, hunger, ignored |
| **Energy** | sleep / idle | constant activity |
| **Boredom** | same signals repeatedly | new discovery, interaction |

Overall **mood** = derived from the dominant stat (priority: starving > very-bored > needy > happy > content > sleepy).

## 4. Faces / animations (LVGL)

Sprite/animation states for the cat:

- `idle` (blinking, tail flick)
- `nom_nom` (eating — triggered on feed, esp. new discovery)
- `happy` (strong signal / played with)
- `hungry` (droopy, belly rumble)
- `bored` (yawn, "meh")
- `sad` / `needy` (big eyes, "where are you?")
- `sleep` (low energy / night)
- evolution morphs between stages

Kawaii style, expressive eyes, smooth transitions. HD within the panel's resolution.

## 5. Evolution

| Stage | Unlock (lifetime unique signals eaten) | Vibe |
|---|---|---|
| 🥚 **Baby** | 0+ | tiny, wide-eyed, learning |
| 🐱 **Teenager** | ~150+ | bigger, cheeky, energetic |
| 🥷 **Pro Hacker Pet** | ~600+ | smug, hoodie, "I've eaten your whole neighbourhood" |

(Thresholds tunable; also gate on age/days alive so it can't insta-evolve in a crowded mall.)

## 6. Personality / messages

Rotating quips based on mood (short, fit the tiny screen), e.g.:

- Hungry: `feed me!`, `so… empty…`, `any wifi nearby??`
- Needy: `oi oi where are you owner`, `play with me!`, `take me for a walk`
- Bored: `same old signals -_-`, `show me somewhere new`
- Happy (new find): `nom nom nom!`, `ooh a fresh one!`, `+1 spectrum`
- Pro flex: `i've tasted 500 networks`

Attention-seeker cadence: if ignored for a while, escalate frequency/urgency of messages (playfully annoying, never spammy-to-the-point-of-hate).

## 7. Persistence

Save to flash (NVS / LittleFS): age (first-boot time), lifetime_unique, evolution stage, best stats, a compact seen[] digest. Restore on boot so the pet "remembers" its life.

## 8. Interaction (depends on hardware)

- If the board has a button/touch: **pet / play** (boosts happiness), **feed toggle**, **wake**.
- Long-press = menu (stats, evolution progress, reset).

## 9. Radios & coexistence (C6)

- WiFi scan + BLE scan share the 2.4 GHz radio via SW coexistence — alternate them, keep windows short.
- 802.15.4 **energy-detect scan** (channels 11-26) = "exotic food"; adds Zigbee/Thread sensing. Phase 2 — needs the IEEE802154/Zigbee API and careful time-slicing with WiFi/BLE. Start WiFi+BLE only.

## Open item (blocking firmware)

**Exact display controller / board model** — needed to bring up LovyanGFX + LVGL correctly (panel driver, resolution, bus, pins, backlight). See README status.
