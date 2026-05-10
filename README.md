# 🐶 Tamagotchi Dog — ESP32 + OLED

A pixel-art animated virtual dog running on an ESP32 with a 0.96" SSD1306 OLED display. Interactable via a light/IR sensor — pet it, double-tap to make it jump, and it sleeps when the room goes dark.

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32 DOIT DevKit V1 |
| Display | SSD1306 0.96" OLED, 128×64, I2C |
| Sensor | IR / Light sensor module (digital out) |

### Wiring

| Signal | GPIO |
|---|---|
| OLED SDA | 21 |
| OLED SCL | 22 |
| OLED VCC | 3.3V |
| OLED GND | GND |
| Sensor DO | 5 |
| Sensor VCC | 3.3V |
| Sensor GND | GND |

> The sensor's onboard potentiometer should be tuned so that:
> - **LOW** = bright room (light hitting sensor freely)
> - **HIGH** = blocked (finger tap / pet, or dark room)

---

## Libraries Required

Install via Arduino IDE Library Manager:

- `Adafruit SSD1306`
- `Adafruit GFX Library`

---

## Behaviours

The dog randomly cycles through three behaviours every 10 seconds:

| Behaviour | Weight | Description |
|---|---|---|
| **Walk** | 60% | Dog walks left and right, tail wagging. Sun shown in background. |
| **Eat** | 20% | Dog walks to a food bowl, nods while eating, bowl slides off when done. |
| **Talk** | 20% | Speech bubble with a woof and mood emoji. Moon and stars shown. |

> **Sleep is not random.** It only triggers when the room goes dark (see below).

---

## Interactions

All interactions use the single sensor pin.

| Action | How | Result |
|---|---|---|
| **Pet** | Hold hand over sensor (HIGH > TAP_MAX_MS) | Heart eyes, floating hearts |
| **Tap** | Quick cover + uncover (HIGH < TAP_MAX_MS) | Heart eyes, floating hearts |
| **Double-tap** | Two taps within DOUBLE_TAP_MS | Dog jumps twice with hearts |
| **Pet while sleeping** | Hold during sleep | Dog briefly lifts head, heart eyes, goes back to sleep |
| **Pet while eating** | Hold during eat | Pauses head nod, heart eyes |

---

## Dark Sleep System

The dog uses the same sensor for both interactions and ambient light detection.

```
Room goes dark (HIGH sustained ≥ 7s)
        │
        ▼
   Dog falls asleep
   (ZZZs float, moon + stars shown)
        │
        ▼
   Every 30s: sample sensor
     ├─ Still HIGH (dark) → keep sleeping, reset 30s timer
     └─ LOW (light detected) ──────────────────────────────┐
                                                           │
                                              Wake up for 20s
                                              (normal behaviours)
                                                           │
                                              After 20s: sample again
                                                ├─ HIGH (dark) → sleep again
                                                └─ LOW (bright) → stay awake
```

### Waking the dog when the room is dark

Since the sensor cannot distinguish a finger from darkness on a single digital pin, waking the dog requires briefly shining a **torch or phone flashlight** directly at the sensor. This drops the pin to LOW, which the 30-second check detects as "light returned" and triggers a 20-second wake window.

---

## Configuration

All timing and behaviour values are `#define` constants at the top of the sketch — easy to change without touching any logic:

```cpp
// Dark sleep
#define DARK_SLEEP_TRIGGER_MS  7000   // ms sustained HIGH → sleep
#define DARK_CHECK_MS         30000   // ms between light checks
#define DARK_WAKE_MS          20000   // ms awake after flashlight

// Behaviour timing
#define BEHAVIOR_MS           10000   // ms before next random behaviour

// Behaviour weights (must sum to 100)
#define WALK_WEIGHT              60   // % chance of WALK
#define EAT_WEIGHT               20   // % chance of EAT
// TALK gets the remainder

// Interaction
#define TAP_MAX_MS              600   // HIGH ≤ this = tap, longer = pet
#define DOUBLE_TAP_MS           500   // gap between taps for double-tap
```

---

## Project Structure

```
tamagotchi-dog/
├── tamagotchi_dog_v7.ino   ← main sketch (copy-paste ready)
└── README.md
```

---

## How It Works — Technical Notes

- **I2C speed** is pushed to 800 kHz (`Wire.setClock(800000)`) for maximum frame rate on the SSD1306.
- **Frame rate** targets ~30 fps (`FRAME_MS = 33`). All animation uses `millis()` — no blocking `delay()` calls anywhere.
- **Edge detection** for the sensor is computed against `lastPinState` which is updated at the *end* of each frame, preventing same-frame race conditions.
- **Sleep discrimination**: a HIGH pulse is only treated as a tap/pet if it *ends* (falls to LOW) within `TAP_MAX_MS`. A HIGH that never falls for `DARK_SLEEP_TRIGGER_MS` is treated as darkness, not a finger.
- **Weighted random** behaviour selection uses a 0–99 roll: 0–59 = WALK, 60–79 = EAT, 80–99 = TALK. Sleep is never in the pool.

---

## Built By

Hrishi — ECE student, R.M.K. Engineering College, Tamil Nadu.  
Part of personal hardware + embedded experiments.
