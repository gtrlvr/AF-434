"""Keyboard jog control for the AF-434 gantry (Arduino Mega 2560 + 4x iCL42).

Upload gantry_control/gantry_control.ino to the Mega first, then run:

    python gantry_keyboard.py            # auto-detects the Mega's COM port
    python gantry_keyboard.py --port COM9
    python gantry_keyboard.py --list-ports

Keys (hold to move, release to decelerate and stop):
    W / S   X axis forward / backward     (motors 1 + 2)
    A / D   Y axis left / right           (motor 3)
    Q / E   Z axis up / down              (motor 4)
    + / -   jog speed up / down
    Space   ramp-stop every axis
    P       print positions
    0       zero positions (only while stopped)
    F / N   free the motors (shafts turn by hand) / enable them again
    C       clear an alarm after fixing its cause
    Esc     ramp-stop everything and quit

The keyboard hook is global: keys are picked up even when another window has
focus, so do not type in other apps while this is running.
"""

import argparse
import sys
import threading
import time

import serial
import serial.tools.list_ports
from pynput import keyboard

BAUD = 115200
EXPECTED_FIRMWARE = "AF434-GANTRY-v1"
HEARTBEAT_INTERVAL_S = 0.1    # firmware ramps to a stop after 0.5 s of silence
SHUTDOWN_WAIT_S = 3.0         # max time to wait for "STOPPED" before exiting

# key -> (axis, serial command).  The firmware uses the same letters.
JOG_KEYS = {
    "w": ("x", "w"), "s": ("x", "s"),
    "d": ("y", "d"), "a": ("y", "a"),
    "q": ("z", "q"), "e": ("z", "e"),
}
AXIS_RELEASE = {"x": "x", "y": "y", "z": "z"}
AXIS_LABEL = {"x": "X", "y": "Y", "z": "Z"}
MOVE_LABEL = {
    "w": "X forward", "s": "X backward",
    "d": "Y right", "a": "Y left",
    "q": "Z up", "e": "Z down",
}
# Shift+= gives '+' on press but may give '=' on release (and vice versa), so
# both spellings map to one logical key.
SPEED_KEYS = {"+": "plus", "=": "plus", "-": "minus", "_": "minus"}

KEY_HELP = """\
  W / S   X forward / backward      + / -   speed up / down
  A / D   Y left / right            Space   stop all
  Q / E   Z up / down               P       print positions
  F / N   free / enable motors      0       zero positions
  C       clear alarm               Esc     stop all and quit
Hold a key to move; release to decelerate. Keys are captured globally.
"""
SIMPLE_KEYS = {"p": "p", "0": "0", "f": "F", "n": "N", "c": "C"}


def normalize(key):
    """Return a logical key name for a pynput key, or None if we don't care."""
    if key == keyboard.Key.esc:
        return "esc"
    if key == keyboard.Key.space:
        return "space"
    char = getattr(key, "char", None)
    if not char:
        return None
    if char in SPEED_KEYS:
        return SPEED_KEYS[char]
    char = char.lower()
    if char in JOG_KEYS or char in SIMPLE_KEYS:
        return char
    return None


class GantryController:
    def __init__(self, connection):
        self.connection = connection
        self.lock = threading.Lock()
        self.held = []          # logical keys currently down, in press order
        self.exiting = False

    # -- serial -------------------------------------------------------------
    def send(self, command):
        # Caller holds self.lock.
        self.connection.write(command.encode("ascii"))

    def heartbeat(self):
        with self.lock:
            if not self.exiting:
                self.send("h")

    def stop_all(self):
        with self.lock:
            self.held.clear()
            self.send("S")

    def begin_exit(self):
        with self.lock:
            self.exiting = True
            self.held.clear()
            self.send("S")

    # -- keyboard -----------------------------------------------------------
    def on_press(self, key):
        name = normalize(key)
        if name is None:
            return None
        if name == "esc":
            print("Esc: ramping everything to a stop...")
            self.begin_exit()
            return False  # stops the listener
        with self.lock:
            if self.exiting or name in self.held:
                return None  # ignore keyboard auto-repeat
            self.held.append(name)
            if name in JOG_KEYS:
                _, command = JOG_KEYS[name]
                self.send(command)
                print(f"{MOVE_LABEL[name]}")
            elif name == "plus":
                self.send("+")
            elif name == "minus":
                self.send("-")
            elif name == "space":
                self.send("S")
                print("Stop all (ramping down)")
            elif name in SIMPLE_KEYS:
                self.send(SIMPLE_KEYS[name])
        return None

    def on_release(self, key):
        name = normalize(key)
        if name is None:
            return
        with self.lock:
            if self.exiting or name not in self.held:
                return
            self.held.remove(name)
            if name not in JOG_KEYS:
                return
            axis, _ = JOG_KEYS[name]
            # If the opposite key on this axis is still held, switch to it;
            # otherwise release the axis so the firmware ramps it down.
            other = next((k for k in reversed(self.held)
                          if k in JOG_KEYS and JOG_KEYS[k][0] == axis), None)
            if other is not None:
                self.send(JOG_KEYS[other][1])
                print(f"{MOVE_LABEL[other]}")
            else:
                self.send(AXIS_RELEASE[axis])
                print(f"{AXIS_LABEL[axis]} released (ramping down)")


def drain_arduino(connection, pending):
    """Print complete lines from the Mega; return any partial line."""
    pending += connection.read(connection.in_waiting)
    lines = pending.split(b"\n")
    for line in lines[:-1]:
        text = line.decode("ascii", errors="replace").strip()
        if text.startswith("ALARM") or text.startswith("ERR"):
            print(f"  [mega] *** {text} ***")
        elif text:
            print(f"  [mega] {text}")
    return lines[-1]


def find_port():
    """Pick the Mega's serial port automatically, or explain why we can't."""
    ports = list(serial.tools.list_ports.comports())
    candidates = [
        p for p in ports
        if (p.vid in (0x2341, 0x2A03, 0x1A86))          # Arduino, Arduino.org, CH340 clones
        or any(tag in (p.description or "") for tag in ("Arduino", "Mega", "CH340", "USB Serial"))
    ]
    if len(candidates) == 1:
        return candidates[0].device
    if not ports:
        raise SystemExit("No serial ports found. Is the Mega plugged in?")
    print("Could not pick a port automatically. Available ports:")
    for p in ports:
        print(f"  {p.device}: {p.description}")
    raise SystemExit("Re-run with --port <name>.")


def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
    for p in ports:
        vid = f"{p.vid:04x}" if p.vid else "----"
        print(f"{p.device}: {p.description} (vid {vid})")


def verify_firmware(connection):
    connection.reset_input_buffer()
    connection.write(b"?")
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        line = connection.readline().decode("ascii", errors="replace").strip()
        if line == EXPECTED_FIRMWARE:
            return line
        if line:
            print(f"  [mega] {line}")
    raise SystemExit(
        f"Did not get firmware id {EXPECTED_FIRMWARE} from the Mega. "
        "Upload gantry_control/gantry_control.ino with the Arduino IDE, close "
        "Serial Monitor, and run this again."
    )


def main(port):
    with serial.Serial(port, BAUD, timeout=0.2, write_timeout=1) as connection:
        time.sleep(2.0)  # the Mega resets when the port opens
        connection.reset_output_buffer()
        firmware = verify_firmware(connection)
        print(f"Connected to {port}: {firmware}")
        print(KEY_HELP)

        controller = GantryController(connection)
        listener = keyboard.Listener(on_press=controller.on_press,
                                     on_release=controller.on_release)
        pending = b""
        try:
            listener.start()
            while listener.is_alive():
                listener.join(HEARTBEAT_INTERVAL_S)
                controller.heartbeat()
                pending = drain_arduino(connection, pending)
        finally:
            listener.stop()
            try:
                controller.begin_exit()
                # Keep heartbeats flowing while the axes decelerate so the
                # firmware's silence timeout isn't what stops them.
                deadline = time.monotonic() + SHUTDOWN_WAIT_S
                stopped = False
                while time.monotonic() < deadline and not stopped:
                    time.sleep(HEARTBEAT_INTERVAL_S)
                    with controller.lock:
                        controller.send("h")
                    pending += connection.read(connection.in_waiting)
                    stopped = b"STOPPED" in pending
                    pending = drain_arduino(connection, pending)
                connection.write(b"p")
                time.sleep(0.1)
                drain_arduino(connection, pending)
            except serial.SerialException:
                pass  # USB gone: the firmware's own timeout ramps everything down
            print("Exited.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Keyboard jog control for the AF-434 gantry.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("--port", help="Serial port of the Mega (default: auto-detect).")
    parser.add_argument("--list-ports", action="store_true", help="List serial ports and exit.")
    args = parser.parse_args()
    if args.list_ports:
        list_ports()
        sys.exit(0)
    port = args.port or find_port()
    try:
        main(port)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        raise SystemExit(f"Serial connection failed on {port}: {exc}") from exc
