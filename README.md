# iCL42 X- and Y-axis control with an Arduino Mega 2560

| Axis | Arduino source | Python keyboard control | Motors / keys |
| --- | --- | --- | --- |
| X | [x_axis_control.txt](x_axis_control.txt) | [keyboard_contro.py](keyboard_contro.py) | Two motors, A/D |
| Y | [y_axis_control.txt](y_axis_control.txt) | [y_axis_keyboard.py](y_axis_keyboard.py) | One motor, Up/Down arrows |

`icl_control.txt` has been renamed to `x_axis_control.txt`. The existing
X-axis direction mapping and firmware version remain the same.
These are **separate, standalone sketches**. On a single Mega, uploading
one replaces the other; they do not control both axes together. Separate
Megas can run the two sketches independently, with different serial ports.

## Y-axis single-motor control

With power disconnected, wire the Y motor as follows. It uses the same
D9/D8 pins as X-axis motor 1.

| Y motor terminal | Connection |
| --- | --- |
| PUL+ | Mega D9 |
| DIR+ | Mega D8 |
| PUL- and DIR- | Mega GND |
| VCC / power + | External 24 V supply positive |
| GND / power - | External supply negative |
| ENA+, ENA-, ALM+, ALM-, RS232 | Leave disconnected |

Power the Mega by USB. Use the same motor settings and PUL/DIR input mode
described below for X. Motor power goes to the external supply, never to
the Mega's signal pins.

1. Paste `y_axis_control.txt` into a new Arduino sketch's `.ino` tab. Select
   **Arduino Mega or Mega 2560**, select its port, and upload.
2. Close Arduino Serial Monitor/Plotter.
3. Install dependencies once with `python -m pip install -r requirements.txt`.
4. Run `python y_axis_keyboard.py --port COM9` (replace COM9 if needed).

Hold **Up** to move up or **Down** to move down; releasing the arrow stops
the motor. If both are held, the last arrow pressed wins, and releasing it
returns to the other held arrow. **Q** increases speed, **E** decreases it,
and **Esc** stops and exits. Keyboard hooks work globally while the script runs.
The motor starts at 500 pulses/sec. Python verifies `AF434-Y-AXIS-v2`
before listening for keys, sends heartbeats every 100 ms, and the sketch
stops pulsing after 500 ms without commands. A heartbeat cannot restart a
stopped motor. Stop ends pulses and leaves the driver holding position.
This sketch does not read end stops or measure position.

Up sets DIR on D8 HIGH; Down sets it LOW. The physical direction depends on
mounting and SW5. If Up moves down, swap `UP_DIR_LEVEL` and `DOWN_DIR_LEVEL`
in `y_axis_control.txt` and upload again.

## X-axis two-motor control

For the current A/D failure investigation and measurement procedure, see
[DIAGNOSTICS.md](DIAGNOSTICS.md).

**Motor 1 is LEFT; motor 2 is RIGHT, mounted facing opposite ways.**
Travel direction maps directly to each motor's own shaft direction:

| Key | Travel | Motor 1 rotation | Motor 2 rotation |
| --- | --- | --- | --- |
| D | Forward | Forward | Backward |
| A | Backward | Backward | Forward |

Hold **A/D** to move and release to stop. **Q/E** changes their shared speed. **Esc** stops
both and exits. When both direction keys are held, the last one pressed wins;
releasing it returns to the other held direction.

## Wiring

The photographed motors are **Leadshine/Leisai iCL42-08 V2.0**. This wiring
uses their PUL+/PUL-, DIR+/DIR- and ENA+/ENA- terminals and the **5 V Mega 2560**.
Each motor already contains its driver and encoder. Use the terminal labels
on your motors, rather than wire colors or an assumed connector orientation.
Disconnect power before wiring.

| Connection | Motor 1 (LEFT) terminal | Motor 2 (RIGHT) terminal |
| --- | --- | --- |
| Mega D9 | PUL+ | — |
| Mega D8 | DIR+ | — |
| Mega D7 | — | PUL+ |
| Mega D6 | — | DIR+ |
| Mega GND | PUL- and DIR- | PUL- and DIR- |
| External 24 V DC supply positive | VCC / power + | VCC / power + |
| External supply negative | GND / power - | GND / power - |
| Leave disconnected | ENA+, ENA-, ALM+, ALM-, RS232 | ENA+, ENA-, ALM+, ALM-, RS232 |

Connect the two motors to the supply **in parallel**. The supply must be rated
for their combined load; use your exact motor specifications to size it.
24 V DC is within the V2.0 motor's specified supply range.
Power the Mega from the computer's USB connection. **Do not connect motor
24 V to the Mega's 5 V, VIN, barrel jack, or signal pins.**

The PUL-/DIR- connections above are the signal return to the Mega's GND.
The motor power GND connections go to the external supply negative. These
are separate circuits through the motor's optically isolated signal inputs;
this wiring does not require an extra link between supply negative and Mega
GND. Keep signal wires short and away from motor power wiring.

This is a common-cathode connection. Each Arduino output drives only one
motor input, whose specified logic current is 7–16 mA. No extra signal
resistor is specified for a 5 V controller. Do not apply this direct wiring
to a 3.3 V board.

## Motor settings

Set both drivers to **the same pulses per revolution** before powering on.
For 1600 pulses/revolution, the photographed V2.0 label and its manual list:

| Switch | Both motors |
| --- | --- |
| SW1 | ON |
| SW2 | OFF |
| SW3 | ON |
| SW4 | ON |
| SW5 | Same direction setting on both motors |
| SW6 | OFF (closed loop) |

Check these settings against the label/manual for your hardware revision.
The V2.0 manual lists 1600 pulses/revolution as the factory setting.
The older STEPPERONLINE series manual uses a different switch table:
OFF/OFF/ON/ON is **3200**, rather than 1600, on your photographed motors.
With 1600 selected, the sketch starts at 500 pulses/sec
(about 18.75 RPM). Q/E adjusts this between about 200 and 16,667 pulses/sec.
Start slowly and increase speed only as the load permits.

The sketch configures each motor's rotation labels independently, then applies
the travel mapping above explicitly. Motor 1 uses `HIGH` for forward and `LOW`
for backward. Motor 2 now uses `HIGH` for forward and `LOW` for backward,
reversing its v7 output after correcting the ENA wiring restored both motors
but left their travel directions opposed. Verify the resulting travel after
uploading v8. These labels refer to the mounting; an iCL42 has
no universal forward travel direction. Verify the resulting physical motion.
The electrical outputs are:

| Key | LEFT direction (Mega D8) | RIGHT direction (Mega D6) |
| --- | --- | --- |
| A (travel backward) | LOW | HIGH |
| D (travel forward) | HIGH | LOW |

SW5 also affects the motor's default direction.
Compare shaft rotation using the same viewing convention for both motors.
If a motor's own forward/backward labels are reversed, swap that motor's
`MOTORx_FORWARD_DIR_LEVEL` and `MOTORx_BACKWARD_DIR_LEVEL` values and re-upload.
If travel direction is reversed for both, swap the levels for both motors.

If either motor keeps rotating the same way when switching A/D, verify that
motor's DIR+ wire reaches its assigned Mega pin (D8 for motor 1, D6 for
motor 2), and that DIR- reaches Mega GND. Disconnect power before checking
wiring. This firmware changes both DIR outputs when travel reverses.

## Upload and run

1. Open `diagnostics/firmware_v8/firmware_v8.ino` in Arduino IDE, or paste all
   of `x_axis_control.txt` into a sketch's `.ino` tab. Select **Arduino Mega or
   Mega 2560** and its port, then upload. The `.ino` export is a v8 snapshot;
   regenerate it from `x_axis_control.txt` if the source changes.
2. Close Arduino Serial Monitor/Plotter so Python can open the port.
3. Set `PORT` in `keyboard_contro.py` to that port (currently `COM9`).
4. In this project directory run:

   ```powershell
   python -m pip install -r requirements.txt
   python keyboard_contro.py
   ```

Python checks for firmware `AF434-LR-DIR-v8` before starting keyboard control.
It stops with an upload instruction if the Mega has an older sketch or does
not respond. A successful start prints `Arduino firmware verified:
AF434-LR-DIR-v8`. Updating the text file alone does not update the Mega;
paste it into the Arduino sketch and upload again.

If Python reports a firmware mismatch, it exits before listening for keys;
A/D cannot control the motors until the uploaded sketch matches Python.
During normal operation, pressing D prints `Travel forward command sent`,
followed by an Arduino `STATE` line. With the default mapping, D reports
`LEFT_D8=1 RIGHT_D6=0 ... RUNNING=1`; A reports `LEFT_D8=0 RIGHT_D6=1`.
This confirms the sketch received a command and enabled pulse generation,
not that the motor shafts moved.

For direction diagnostics, run `python keyboard_contro.py --calibrate`.
R flips the right motor's direction mapping and stops both motors; resetting
the Mega restores the default. Keys 1/2 set the left DIR output LOW/HIGH,
and 3/4 set the right DIR output LOW/HIGH without sending pulses. After a
calibration command, release all A/D and calibration keys before moving again.

If both motors reverse on A/D but travel in opposite directions, release
A/D, press and release R, then try A/D again. If this fixes movement, save it
by swapping `MOTOR2_FORWARD_DIR_LEVEL` and `MOTOR2_BACKWARD_DIR_LEVEL` in the
sketch and re-uploading. `RIGHT_FLIPPED=1` reports a temporary change, which
is lost when the Mega resets.

If a motor does **not** reverse on A/D, use keys 1-4 to test its DIR wiring:

| Key | Arduino output | Motion |
| --- | --- | --- |
| 1 | LEFT D8 LOW | Both stopped |
| 2 | LEFT D8 HIGH | Both stopped |
| 3 | RIGHT D6 LOW | Both stopped |
| 4 | RIGHT D6 HIGH | Both stopped |

These states remain set after key release. With a multimeter on DC volts,
measure the selected Mega pin relative to Mega GND: LOW should be near 0 V,
HIGH near 5 V. Then measure **DIR+ relative to DIR- at that motor**; it should
change between the same levels. Both motors remain stopped in these tests.
Restarting A/D restores the configured motion direction signals.

For the right motor, trace **Mega D6 -> terminal 3 (DIR+)** and
**Mega GND -> terminal 4 (DIR-)**. For the left motor, use D8 -> terminal 3
and Mega GND -> terminal 4. Follow printed labels rather than wire colors.
Disconnect power before correcting connections. Other main terminals are
1=PUL+, 2=PUL-, 9=power GND, 10=power VCC.

If D6 changes but the voltage across right DIR+/DIR- does not, check the
wire, connection, and return path. If the motor's DIR voltage changes but
rotation still does not, check that its configured pulse input mode is
**PUL/DIR**, rather than **CW/CCW**. The V2.0 manual lists pulse input mode
0=PUL/DIR and 1=CW/CCW. SW6 selects closed/open loop; it is not the pulse mode.
If A/D is pressed while this guard is active, Python prints `Motion blocked
by calibration` and sends no motion command.

The script listens to keys globally, including while other windows have
focus. Both motors receive one pulse per step; their edges are separated by
a few microseconds from sequential GPIO writes. Identical microstep settings
give the same commanded angular distance, but the Arduino does not read encoder
positions or motor alarms.

Python sends a heartbeat every 100 ms. The sketch stops pulsing both motors
after 500 ms without a valid command, and Python sends a stop on exit.
A heartbeat alone never restarts motion after a timeout: release and press
A/D again. Stopping pulses leaves the drivers enabled and holding position.

Sources: [Leadshine/Leisai iCL42-08 V2.0 manual](https://www.leisaishop.com/uploadfiles/iCL42-08%20(V2.0)%E4%B8%80%E4%BD%93%E5%BC%8F%E9%97%AD%E7%8E%AF%E6%AD%A5%E8%BF%9B%E7%94%B5%E6%9C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E%E4%B9%A6%20V1.00.pdf),
[older STEPPERONLINE series manual](https://www.slideshare.net/slideshow/iclseries-closed-stepper-motor-manualpdf/262787142),
[iCL42-08 specifications](https://www.omc-stepperonline.com/icl-series-nema-17-integrated-closed-loop-stepper-motor-0-8nm-113-29oz-in-20-36vdc-w-14-bit-encoder-icl42-08),
and [Arduino Mega 2560 pinout](https://docs.arduino.cc/resources/pinouts/A000067-full-pinout.pdf).
