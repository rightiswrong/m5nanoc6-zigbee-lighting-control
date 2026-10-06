# Wiring

## 1. The basic string (no hot-plug detection)

Grove cables are straight-through; colours are the standard Grove scheme.

```
 M5NanoC6 Grove port                Grove Chainable RGB LED v2.0
 ┌──────────────┐   Grove cable    ┌──────┐ IN      OUT ┌──────┐ IN      OUT
 │ G1  yellow ──┼─────────────────►│ CI   │────────────►│ CI   │──► …
 │ G2  white  ──┼─────────────────►│ DI   │  (Grove     │ DI   │
 │ 5V  red    ──┼─────────────────►│ VCC  │   cable)    │ VCC  │
 │ GND black  ──┼─────────────────►│ GND  │────────────►│ GND  │
 └──────────────┘                  └──────┘  module #1  └──────┘  module #2
```

Set `CHAIN_DETECT_LOOPBACK 0` in `config.h`, then tell the firmware how many
modules you have with the **Manual LED count** control in Home Assistant.

## 2. Hot-plug detection: the loopback ("return wire")

A P9813 chain never talks back, so the firmware can only count modules if the
**last module's data output comes home**. The NanoC6 has no spare GPIO, so the
return signal shares G2 with the outgoing data through two resistors.

```
                       ┌──────────── sense tap (at the NanoC6) ────────────┐
 NanoC6 G2 (white) ────┼──┬────────────────────────────────────────────────┼──► module #1 DI
                       │  │                                                 │
                       │  ├── 18 kΩ ── GND (black)                          │
                       │  │                                                 │
                       │  └── 10 kΩ ──┐                                     │
                       └──────────────┼─────────────────────────────────────┘
                                      │  return wire (any thin wire)
                                      │
   … module #N-1 ──► module #N OUT ─ white (DO) ┘     ← the "terminator" cable
                                    yellow (CO)  → leave unconnected, insulate
                                    red / black  → leave unconnected
```

* **Why the 10 kΩ?** While the NanoC6 is *sending*, G2 is driven hard and the
  10 kΩ stops the returning signal from fighting it (≤0.5 mA).
* **Why the 18 kΩ?** While the NanoC6 is *listening*, it divides the module's
  5 V logic down to 5 × 18/28 ≈ 3.2 V — safe for the ESP32-C6, which is **not**
  5 V tolerant. It also pulls the line low when no return wire is attached.
* **Never** connect the return wire straight to G2 without the divider.

### Adding or removing a module

Plug the new module onto the end of the string and move the **terminator
cable** to its OUT socket (or remove a module and move the terminator back).
Within ~3 seconds the firmware sees the new count, wakes the new module with a
pale shimmer, re-flows the running effect across all pixels, and reports the
count to Home Assistant (which fires a `Chain change` event).

> Tip: make the terminator from a Grove cable with the yellow/red/black wires
> clipped and the white wire spliced to a long thin return lead. A
> pre-made Grove → female-jumper cable also works.

### Parts for the sense tap

| Part | Qty |
|---|---|
| 10 kΩ resistor, ¼ W | 1 |
| 18 kΩ resistor, ¼ W (15–22 kΩ also fine) | 1 |
| Grove screw-terminal board, or a spliced Grove cable | 1 |
| Thin hook-up wire for the return run | length of your string |

## 3. Power

The NanoC6 passes USB 5 V to the Grove port. Each module can draw ~60 mA at full
white; the firmware estimates draw every frame and dims uniformly to stay under
`POWER_BUDGET_MA` (1000 mA by default). Use a solid 5 V / 2 A USB supply.

For long strings (more than ~15 modules), give the chain its own 5 V supply:
cut the **red** wire between the NanoC6 and module #1, connect the external
supply's 5 V to the module side, and join the supply's GND to the black wire.
Then raise `POWER_BUDGET_MA` to match that supply. Never tie two 5 V supplies
together.

## 4. Onboard LED

Nothing to wire: the NanoC6's WS2812 is on G20 behind a load switch on G19,
which the firmware enables at boot.
