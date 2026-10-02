# AF-434 gantry control

Keyboard jog control for a 3-axis gantry driven by four **Leadshine iCL42**
integrated closed-loop steppers (driver and 14-bit encoder built into each
motor) and an **Arduino Mega 2560**.

| File | What it is |
| --- | --- |
| [gantry_control/gantry_control.ino](gantry_control/gantry_control.ino) | Firmware for the Mega. Generates step pulses for all four motors with smooth acceleration and deceleration. |
| [gantry_keyboard.py](gantry_keyboard.py) | Python keyboard controller. Talks to the Mega over USB serial. |
| [requirements.txt](requirements.txt) | Python dependencies (`pyserial`, `pynput`). |

## Axes and motors

| Axis | Keys | Motor | Position | Positive direction | Shaft rotation |
| --- | --- | --- | --- | --- | --- |
| X | W forward / S backward | 1 | left side | forward | forward = CW, backward = CCW |
| X | W forward / S backward | 2 | right side, mounted mirrored | forward | forward = CCW, backward = CW |
| Y | D right / A left | 3 | | right | right = CW, left = CCW |
| Z | Q up / E down | 4 | | up | up = CCW, down = CW |

Motors 1 and 2 always receive identical pulse trains with opposite DIR
levels, so the X gantry beam moves as one piece.

## Wiring

Disconnect all power before wiring. Follow the printed terminal labels on the
motors, not wire colours. This is common-cathode wiring on the Mega's 5 V
logic, the same arrangement that already worked for motors 1 and 2.

| Mega pin | Motor terminal |
| --- | --- |
| D9 | Motor 1 PUL+ |
| D8 | Motor 1 DIR+ |
| D7 | Motor 2 PUL+ |
| D6 | Motor 2 DIR+ |
| D5 | Motor 3 PUL+ |
| D4 | Motor 3 DIR+ |
| D3 | Motor 4 PUL+ |
| D2 | Motor 4 DIR+ |
| D22 / D23 / D24 / D25 | ENA+ of motor 1 / 2 / 3 / 4 |
| D26 / D27 / D28 / D29 | ALM+ of motor 1 / 2 / 3 / 4 |
| GND | PUL-, DIR-, ENA- and ALM- of **all four** motors (16 wires; a small GND terminal strip tied to one Mega GND pin keeps this tidy) |

| Motor terminal | Connection |
| --- | --- |
| VCC / power + | External 24 V DC supply positive (all four motors in parallel) |
| GND / power - | External supply negative |
| RS232 | Leave disconnected |

ENA is held in the enabled state by the Mega, so the motors hold torque from
power-up. Press F to free all four (shafts turn by hand) and N to enable them
again. ALM is the driver's open-collector alarm output. If any driver raises
it (over-current, following error, and so on) the Mega hard-stops every axis,
prints `ALARM Mn`, and refuses motion until you fix the cause and press C.

- The Mega is powered from USB only. Never connect the 24 V supply to the
  Mega's VIN, 5 V, barrel jack or any signal pin.
- Size the 24 V supply for four motors running at once. Check the current
  rating on your exact iCL42 variant.
- The signal returns (PUL-/DIR-) go to Mega GND. The motor power returns go to
  the supply negative. The motor inputs are opto-isolated, so no extra link
  between the two grounds is needed. Keep signal wires short and away from the
  motor power leads.
- Several GND pins on the Mega are fine to use. If you run out, a small
  terminal strip or breadboard rail tied to one Mega GND pin works.

### Driver DIP switches

Set every motor identically before powering on. For 1600 pulses/rev on the
iCL42-08 V2.0 (the factory default): SW1 ON, SW2 OFF, SW3 ON, SW4 ON. SW5
sets the default rotation direction and must be the same on all four. SW6 OFF
selects closed loop. Confirm against the label on your motors; older iCL
revisions use a different microstep table. The pulse input mode must be
PUL/DIR, not CW/CCW.

## Upload the firmware

1. Open `gantry_control/gantry_control.ino` in the Arduino IDE.
2. Tools > Board > **Arduino Mega or Mega 2560**, then pick its port.
3. Upload. Close Serial Monitor afterwards so Python can open the port.

## Run the keyboard controller

```powershell
python -m pip install -r requirements.txt
python gantry_keyboard.py
```

The script finds the Mega's port by itself. If it cannot, use
`python gantry_keyboard.py --list-ports` and then `--port COMx`.

| Key | Action |
| --- | --- |
| W / S | X axis forward / backward |
| A / D | Y axis left / right |
| Q / E | Z axis up / down |
| + / - | Jog speed up / down (x1.25 per press, 100 to 8000 steps/s) |
| Space | Ramp every axis to a stop |
| P | Print positions and velocities |
| 0 | Zero the position counters (only while stopped) |
| F / N | Free all motors (no holding torque) / enable them again |
| C | Clear the alarm latch after fixing what tripped it |
| Esc | Ramp every axis to a stop, then quit |

Hold a key to move, release it to stop. Every start, stop and reversal is a
velocity ramp, so nothing jerks. If both keys of one axis are held the most
recent one wins, and releasing it hands control back to the other. All three
axes can move at once. The keyboard hook is global: it also sees keys typed
into other windows, so stop the script before typing elsewhere.

Python sends a heartbeat every 100 ms. If the Mega hears nothing for 500 ms
(cable unplugged, script crashed) it ramps every axis to a stop by itself.

## Checking directions on first power-up

The firmware assumes DIR HIGH = clockwise. That depends on SW5, so verify it
with the gantry clear of obstacles and the speed turned down (press `-` a few
times first):

1. Tap W. Motor 1 should turn CW and motor 2 CCW, moving the X beam forward.
2. Tap D. Motor 3 should turn CW, moving right.
3. Tap Q. Motor 4 should turn CCW, moving up.

If **every** motor runs backwards, swap `CW_LEVEL` and `CCW_LEVEL` near the
top of the sketch. If only one motor or one axis is wrong, flip that motor's
`Mx_POSITIVE_IS_CW` constant. Re-upload after either change. On this gantry
the X pair needed flipping, so motors 1 and 2 are already set opposite to
the table above in the sketch.

Two more polarity checks, both visible before anything moves:

- Right after power-up the shafts should resist being turned by hand. If they
  spin freely, the ENA polarity is reversed: swap `ENA_ENABLED_LEVEL` and
  `ENA_DISABLED_LEVEL`.
- Press P. The status line should end with `ALARM= none`. If it lists every
  motor while they are clearly fine, the ALM polarity is reversed: flip
  `ALM_ACTIVE_LEVEL` between `HIGH` and `LOW`. On this gantry's motors the
  correct value is `HIGH` (the ALM output conducts during normal operation).

## Tuning

All tuning lives at the top of the sketch:

| Constant | Default | Meaning |
| --- | --- | --- |
| `DEFAULT_SPEED` | 1000 steps/s | Jog speed at power-up (37.5 RPM at 1600 pulses/rev) |
| `MIN_SPEED` / `MAX_SPEED` | 100 / 8000 | Limits for the + and - keys |
| `ACCEL` | 3000 steps/s² | Ramp rate. Lower = gentler, longer stops. 1000 steps/s stops in 0.33 s and 167 steps. |
| `CRAWL_SPEED` | 150 steps/s | Final approach speed for go-to moves |

## Serial protocol

The Python script is a thin keyboard wrapper. Anything that can open a serial
port at 115200 baud can drive the gantry with these commands.

| Send | Effect |
| --- | --- |
| `w` `s` `d` `a` `q` `e` | Start jogging an axis (same meaning as the keys) |
| `x` `y` `z` | Release that axis: ramp to a stop |
| `+` `-` | Change jog speed. Replies `SPEED n` |
| `S` | Ramp every axis to a stop |
| `!` | Emergency stop, no ramp. Replies `ESTOP` |
| `h` | Heartbeat. Send at least every 500 ms while moving |
| `?` | Replies `AF434-GANTRY-v1` |
| `p` | Replies `POS X=.. Y=.. Z=.. VEL X=.. Y=.. Z=.. SPEED=.. ENABLED=1 ALARM= none` |
| `0` | Zero all positions. Replies `ZEROED` |
| `N` / `F` | Enable all drivers / free them (F only while stopped). Replies `ENABLED` or `FREE` |
| `C` | Clear the alarm latch. Replies `ALARM CLEARED` or `ALARM still active: Mn` |
| `GX1600` + newline | Go to absolute step position on an axis (also `GY`, `GZ`, negatives allowed). Replies `GOTO X 1600`, then `DONE X` on arrival. Accelerates, cruises at the jog speed, and decelerates onto the target. |

The Mega prints `STOPPED` whenever all axes come to rest and `ALARM Mn` the
moment a driver faults. Positions are counted from commanded steps, not read
back from the encoders. The iCL42 closes its own loop internally; the Mega
does not read the encoder or the RS232 port. Freeing the motors with F and
moving them by hand makes the step count wrong, so press 0 at a known spot
afterwards.
