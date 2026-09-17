# iCL42 A/D troubleshooting

## Evidence and current limits

The user reports that correcting the ENA wiring restored both motors for A/D.
Both still moved in opposing travel directions with v7. The v8 mapping now
reverses motor 2's DIR level for both travel commands; physical verification
of coordinated travel is still needed after upload.

The v8 sketch was compiled and uploaded to COM9. A real stop/status query
verified the current Python expectation and the board both report v8:

```text
AF434-LR-DIR-v8
STATE LEFT_D8=1 RIGHT_D6=0 RIGHT_FLIPPED=0 RUNNING=0
```

This confirms the uploaded default reverses motor 2's DIR GPIO level relative
to v7 while pulse generation is stopped. Test physical travel with the
restarted Python controller; the verification query did not command motion.

Before that wiring correction, D moved neither motor and A moved only motor 1.
The user supplied real v7 A/D terminal logs. Physical wiring, driver
switch positions, input voltages, pulse trains, alarm indicators, and stored
pulse-input modes have not been independently measured. The motion failure
was resolved by the user's wiring correction; the remaining task is direction
calibration.

The historical supplied logs repeatedly show firmware v7, RIGHT_FLIPPED=0, and:

```text
D: STATE LEFT_D8=1 RIGHT_D6=1 RIGHT_FLIPPED=0 RUNNING=1
A: STATE LEFT_D8=0 RIGHT_D6=0 RIGHT_FLIPPED=0 RUNNING=1
```

This establishes that the real keyboard/serial command path works and both
DIR GPIO levels change as configured. A firmware-version mismatch or blocked
calibration key handler does not explain these logged motion commands. The
user subsequently restored both motors by correcting the ENA wiring. The
following measurements remain useful if a motion or direction failure returns.

The README was updated during this audit to identify photographed motors as
Leadshine/Leisai iCL42-08 V2.0. This audit has not independently inspected those
photos or the motor labels. Confirm that identification for both motors; the
V2.0 and older series manuals have different configuration information.

The current code sends `d` for D and `a` for A. Both commands enable the same
pulse-generation block, which writes a pulse to D9 and D7 every stepping cycle.
There is no normal command branch that intentionally runs only motor 1.
This does not prove that either pulse reaches the motor input.

A previous real serial query found v6 on COM9 while Python expected v7. The v7
sketch was compiled and uploaded, and a subsequent query verified:

```text
AF434-LR-DIR-v7
STATE LEFT_D8=1 RIGHT_D6=1 RIGHT_FLIPPED=0 RUNNING=0
```

That query verified a stopped board and matching firmware at that time. It did
not exercise motion. The subsequently supplied user logs establish A/D receipt
and DIR readback during motion requests, but still do not measure motor inputs.

Mocked checks passed for direction-key handling, overlapping A/D keys,
auto-repeat, Esc shutdown, calibration blocking, partial serial replies, and
matching/mismatched firmware startup. They do not test physical keys or motors.

## 1. Locate the failure using terminal messages

Close Arduino Serial Monitor/Plotter, then run normal control:

```powershell
python keyboard_contro.py
```

The current startup message must be `Arduino firmware verified: AF434-LR-DIR-v8`.
A mismatch exits the script before keyboard listening starts. If the process
has exited, pressing A/D cannot send a command.

Hold D briefly, release it, then hold A briefly and release it. Copy the complete
terminal output. With the default mapping, expect:

```text
Travel forward command sent
Arduino: STATE LEFT_D8=1 RIGHT_D6=0 RIGHT_FLIPPED=0 RUNNING=1
Travel backward command sent
Arduino: STATE LEFT_D8=0 RIGHT_D6=1 RIGHT_FLIPPED=0 RUNNING=1
```

There can also be a startup STATE line with RUNNING=0 and stop messages.

| Observation | What it establishes / next check |
| --- | --- |
| No command-sent message | Python did not send a new motion command. Check startup, process errors, full key release, and calibration blocking. |
| Command-sent message without a corresponding STATE | Python wrote the command, but board receipt has not been established. Check serial errors, uploaded firmware, and whether the script remains running. |
| Expected STATE, RUNNING=1, motor stationary | The board handled a motion command and read back its DIR GPIO levels. Measure pulses, wiring, input levels, motor power, and alarms next. |
| RIGHT_FLIPPED=1 | The right direction override is active. Expected D/A levels on D6 are reversed from the examples above. |

RUNNING=1 is a software flag. It does not report motor movement or independently
measure pulse generation. The GPIO STATE report cannot establish terminal
voltages or cable continuity.

## 2. Trace the wiring with power disconnected

This is the intended common-cathode wiring, not a confirmed cable inspection:

| Motor terminal | Motor 1 | Motor 2 |
| --- | --- | --- |
| PUL+ | Mega D9 | Mega D7 |
| PUL- | Mega GND | Mega GND |
| DIR+ | Mega D8 | Mega D6 |
| DIR- | Mega GND | Mega GND |
| VCC / power + | External +24 V | External +24 V |
| GND / power - | External supply negative | External supply negative |
| ENA+, ENA- | Unconnected | Unconnected |
| ALM+, ALM-, RS232 | Unused | Unused |

The motor power connections are in parallel; the Mega is powered by USB.
Do not connect motor 24 V to Mega signal or 5 V pins. The signal returns above
go to Mega GND, rather than being assumed interchangeable with motor power GND.

Follow terminal labels on the actual motors. Check both PUL and DIR pairs for
swaps, broken/loose connections, incorrect pin numbers, and unintended ENA
connections. Check that one motor has not instead been wired common-anode
with its positive signal terminals tied to a supply. Do not mix that topology
with this firmware's documented wiring without reviewing the full circuit.

Prioritize tracing D8 and D6 to the printed DIR+ terminals, rather than ENA+.
Those are different inputs: an accidentally connected enable input can change
driver enable state instead of rotation direction. This is a wiring hypothesis
to inspect, not a diagnosis established by the current logs. Then trace D9/D7
to the respective PUL+ terminals and verify both negative signal returns.

## 3. Measure DIR levels without pulses

Exit normal control, then run:

```powershell
python keyboard_contro.py --calibrate
```

Press and release each test key separately:

| Key | Output set | Expected voltage to Mega GND |
| --- | --- | --- |
| 1 | D8 LOW | Approximately 0 V |
| 2 | D8 HIGH | Approximately 5 V |
| 3 | D6 LOW | Approximately 0 V |
| 4 | D6 HIGH | Approximately 5 V |

These commands stop both pulse outputs. A multimeter is suitable for these
static measurements. After a test command, release all A/D, R, and 1-4 keys
before requesting motion. The updated Python script explains when this guard
blocks a motion command.

First measure D8/D6 to Mega GND. Then measure directly across the corresponding
motor's DIR+ and DIR- terminals while the normal signal wiring is connected.
This checks the level delivered through the cable under the input load.

If a level changes at the Mega but not across the motor input, investigate the
cable, terminal assignment, and return connection. If the HIGH voltage collapses
when the motor input is connected, inspect the load, wiring, and signal-drive
requirements. Digital HIGH readback alone does not prove a valid motor input.

The cited iCL manual specifies signal HIGH as 4.5-5 V and LOW as 0-0.5 V;
confirm this for the exact motor revision. Do not change direction constants to
compensate for an input that never receives the correct electrical levels.

## 4. Verify the pulse outputs

After confirming the signal returns, use an oscilloscope at D9 and D7 referenced
to Mega GND, or a logic analyzer whose inputs tolerate the signal voltage.
In normal control, check both outputs while separately holding A and D.

The code targets about 500 pulses/second at its initial speed. It holds both
outputs HIGH across a 5-microsecond delay; actual HIGH time also includes GPIO
write overhead. Both pulse outputs should operate for either direction command
and stop after release. Serial reporting can introduce timing variation.

Check the pulse at each motor's PUL+ relative to PUL- after confirming that PUL-
is the documented Mega GND return. A logic analyzer verifies transitions but
does not establish the loaded HIGH voltage; a scope can check both.

If pulses exist at a Mega pin but are absent at that motor input, the signal
path is suspect. If D9 pulses but D7 does not at the board, investigate the
D7 channel/pin, its connected load, and actual uploaded firmware.

A multimeter average voltage is not proof of a valid pulse train: these pulses
have a low duty cycle. The cited manual's minimum pulse width is 2.5 microseconds
and minimum DIR setup time is 5 microseconds; the code uses a 5-microsecond
pulse delay and 10-microsecond direction setup delay.

## 5. Check motor power, driver state, and settings

Record each motor's full model suffix, hardware revision if shown, red alarm
LED pattern, and actual SW1-SW6 positions. Check the 24 V feed at each motor's
VCC/power-GND terminals, including whether it drops during a motion request.
Confirm the supply is suitable for the combined load and both power connections
are secure. A power or alarm problem is a hypothesis until these are measured.

Both cited manuals list SW1-SW4 as microstep selection, SW5 as default
rotation direction, and SW6 as open/closed loop. Their 1600-pulse settings differ:

| Manual / hardware | SW1 | SW2 | SW3 | SW4 |
| --- | --- | --- | --- | --- |
| Leadshine/Leisai iCL42-08 V2.0 | ON | OFF | ON | ON |
| Older STEPPERONLINE iCL series | OFF | OFF | ON | ON |

Closed loop uses SW6 OFF in the tables. Verify against the actual motor's
label/manual before changing anything. Do not apply a separate CL42T driver's
switch table to an integrated iCL42 motor.

For V2.0, also read the driver's stored **pulse input mode** using its appropriate
vendor tuning software and RS232 interface. The manual's parameter table lists
`0 = PUL/DIR` and `1 = CW/CCW`; this Arduino firmware requires `0 = PUL/DIR`.
CW/CCW uses separate direction-specific pulse inputs rather than the step stream
plus static direction level that this sketch sends. A wrong mode is a plausible
reason for unexpected response, not a confirmed diagnosis. SW6 selects loop
mode, not pulse input mode. Record the current parameter before changing it.
This parameter is not readable through the current Mega firmware; the motor
tuning ports are documented as unconnected. See the
[V2.0 manual's parameter table, printed page 9](https://www.leisaishop.com/uploadfiles/iCL42-08%20(V2.0)%E4%B8%80%E4%BD%93%E5%BC%8F%E9%97%AD%E7%8E%AF%E6%AD%A5%E8%BF%9B%E7%94%B5%E6%9C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E%E4%B9%A6%20V1.00.pdf).

## 6. Separate a signal-path fault from a motor/power/load fault

For this comparison, unload the mechanism so testing a motor separately does
not drive against its paired motor. Power down before every cable exchange.

Use the previously working motor 1 signal channel (D9 pulse, D8 direction,
and the documented signal returns) to test motor 2 alone. Disconnect the
other motor's control signals; never connect two Arduino output pins together.
Keep motor power connections correctly assigned. Use the same A command that
previously moved motor 1, then stop.

Next test motor 1 alone using the original motor 2 signal channel/cable
(D7 pulse, D6 direction, and returns), again changing cables with power off.

| Result | Interpretation |
| --- | --- |
| Motor 2 works on the known-working channel; motor 1 fails on the original motor 2 path | The failure follows the signal path: investigate its Arduino output, wiring, cable, and input voltage. |
| Motor 2 still fails on the known-working channel; motor 1 works on either path | Investigate motor 2's power feed, enable/alarm state, driver/input, or mechanical load. This does not by itself prove a damaged motor. |
| Results depend on direction or differ from those combinations | Record exact commands, pin/terminal measurements, and alarm state; the fault has not yet been isolated. |

Do not exchange internal motor phase or encoder wiring on these integrated
units. This comparison concerns the external control channel only.

## 7. Calibrate travel direction after electrical operation is established

Observe both rotations using a consistent viewing convention and record which
movement drives the mechanism forward. Opposite-facing shaft observations can
have opposite clockwise/counterclockwise descriptions for coordinated travel.
Connector orientation does not identify the required polarity.

In calibration mode, R flips motor 2's direction mapping and stops both motors.
Release every motion/calibration key before another A/D test. Reset restores
the sketch default. Choose permanent direction levels from successful measured
operation and actual travel, rather than repeating inversion guesses.

## Software files and upload

The current [Python controller](keyboard_contro.py) now prints a warning when
calibration blocks A/D. This feedback change requires restarting Python and
does not require a firmware upload.

The X-axis Arduino source is [x_axis_control.txt](x_axis_control.txt). The complete v8
`.ino` export is in [diagnostics/firmware_v8/firmware_v8.ino](diagnostics/firmware_v8/firmware_v8.ino).
It includes the motor 2 direction correction. The older firmware_v7 export
is historical and does not match the current Python firmware check. If the
source is changed later, regenerate the current export.

To upload the export, open its `.ino` file in Arduino IDE, select Arduino Mega
or Mega 2560 and COM9, and upload. Close Serial Monitor/Plotter before Python.
On restart, verify the firmware identifier matches the Python expectation.
Updating a local text file does not update the board.

Startup/A/D terminal logs were supplied for v7 and matched its GPIO states.
The next observation needed is whether v8 makes both motors drive the mechanism
together in each direction. If motion fails again, collect the current STATE
lines and use the signal, power, alarm, and controlled swap checks above.

Sources: [Leadshine/Leisai iCL42-08 V2.0 manual](https://www.leisaishop.com/uploadfiles/iCL42-08%20(V2.0)%E4%B8%80%E4%BD%93%E5%BC%8F%E9%97%AD%E7%8E%AF%E6%AD%A5%E8%BF%9B%E7%94%B5%E6%9C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E%E4%B9%A6%20V1.00.pdf)
and [older STEPPERONLINE-uploaded iCL series manual](https://www.slideshare.net/slideshow/iclseries-closed-stepper-motor-manualpdf/262787142).
The exact hardware revision must still be confirmed.
