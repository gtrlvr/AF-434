"""X-axis keyboard -> Mega 2560 bridge for two iCL42 motors moving together.

Motor 1 = LEFT, motor 2 = RIGHT, mounted facing opposite ways.
Hold D = travel forward: motor 1 forward, motor 2 backward.
Hold A = travel backward: motor 1 backward, motor 2 forward.
Release = stop both.
Q/E = shared speed up/down, Esc = stop both and quit.
Install dependencies with: python -m pip install -r requirements.txt
For direction diagnostics: python keyboard_contro.py --calibrate
"""

import argparse
import threading
import time

import serial
from pynput import keyboard

PORT = "COM9"  # Change to the Mega's port shown in Arduino IDE.
BAUD = 115200
EXPECTED_FIRMWARE = "AF434-LR-DIR-v8"
HEARTBEAT_INTERVAL_S = 0.1  # Firmware stops after 0.5 s without commands.


class KeyboardController:
    def __init__(self, connection, calibrate=False):
        self.connection = connection
        self.calibrate = calibrate
        self.calibration_stopped = False
        self.held = []  # Press order: the most recently pressed A/D wins.
        self.lock = threading.Lock()
        self.exiting = False

    def send(self, command):
        # Caller holds the lock so heartbeat and key callbacks cannot overlap.
        self.connection.write(command.encode("ascii"))

    def heartbeat(self):
        with self.lock:
            if not self.exiting:
                self.send("h")

    def stop(self):
        with self.lock:
            self.exiting = True
            self.held.clear()
            self.send("s")

    def on_press(self, key):
        if key == keyboard.Key.esc:
            self.stop()
            return False
        char = getattr(key, "char", None)
        if not char:
            return
        char = char.lower()
        allowed = ("a", "d", "q", "e")
        if self.calibrate:
            allowed += ("r", "1", "2", "3", "4")
        if char not in allowed:
            return
        with self.lock:
            if self.exiting or char in self.held:
                return  # Ignore keyboard auto-repeat.
            self.held.append(char)
            if char in ("r", "1", "2", "3", "4"):
                self.calibration_stopped = True
            elif char in ("a", "d") and self.calibration_stopped:
                print("Motion blocked by calibration: release all A/D, R, and 1-4 keys, "
                      "then press A/D again.")
                return  # Release all motion/calibration keys before restarting.
            self.send(char)
            print({"a": "Travel backward command sent",
                   "d": "Travel forward command sent",
                   "q": "Both speed +", "e": "Both speed -",
                   "r": "RIGHT direction flipped; both stopped. Release all A/D and R keys before moving.",
                   "1": "LEFT D8 LOW; both stopped",
                   "2": "LEFT D8 HIGH; both stopped",
                   "3": "RIGHT D6 LOW; both stopped",
                   "4": "RIGHT D6 HIGH; both stopped"}[char])

    def on_release(self, key):
        char = getattr(key, "char", None)
        if not char:
            return
        char = char.lower()
        with self.lock:
            if self.exiting or char not in self.held:
                return
            self.held.remove(char)
            if self.calibration_stopped:
                if not any(k in self.held for k in ("a", "d", "r", "1", "2", "3", "4")):
                    self.calibration_stopped = False
                return
            if char in ("a", "d"):
                direction = next((k for k in reversed(self.held)
                                  if k in ("a", "d")), "s")
                self.send(direction)
                if direction == "s":
                    print("Both stopped")


def print_arduino_messages(connection, pending):
    # Read available bytes only: partial lines must not delay the heartbeat.
    pending += connection.read(connection.in_waiting)
    lines = pending.split(b"\n")
    for line in lines[:-1]:
        print(f"Arduino: {line.decode('ascii', errors='replace').strip()}")
    return lines[-1]


def main(calibrate=False):
    with serial.Serial(PORT, BAUD, timeout=1, write_timeout=1) as connection:
        controller = KeyboardController(connection, calibrate=calibrate)
        listener = None
        try:
            time.sleep(2)  # Let the Mega reset after opening USB serial.
            connection.reset_output_buffer()
            connection.reset_input_buffer()
            with controller.lock:
                controller.send("s")
                controller.send("?")
            firmware = connection.readline().decode("ascii", errors="replace").strip()
            if firmware != EXPECTED_FIRMWARE:
                raise SystemExit(
                    f"Expected Arduino firmware {EXPECTED_FIRMWARE}; received "
                    f"{firmware or 'no response'}. Upload the current x_axis_control.txt "
                    "as an Arduino sketch to the Mega, then restart Python."
                )
            print(f"Arduino firmware verified: {firmware}")
            print("Motor 1 = LEFT | Motor 2 = RIGHT")
            print("D = travel forward: motor 1 forward + motor 2 backward")
            print("A = travel backward: motor 1 backward + motor 2 forward")
            print("Release A/D = stop | Q/E = shared speed +/- | Esc = quit")
            if calibrate:
                print("R = flip RIGHT direction and stop (reset restores the sketch default)")
                print("1/2 = LEFT DIR LOW/HIGH | 3/4 = RIGHT DIR LOW/HIGH | no movement")
            pending = b""
            listener = keyboard.Listener(on_press=controller.on_press,
                                         on_release=controller.on_release)
            listener.start()
            while listener.is_alive():
                listener.join(HEARTBEAT_INTERVAL_S)
                if listener.is_alive():
                    controller.heartbeat()
                    pending = print_arduino_messages(connection, pending)
        finally:
            if listener is not None:
                listener.stop()
            try:
                controller.stop()
            except serial.SerialException:
                pass  # If USB is gone, the firmware's timeout stops both motors.
            print("Both stopped. Exiting.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--calibrate", action="store_true",
                        help="Enable R to flip the right motor, and 1-4 to test DIR outputs without motion.")
    args = parser.parse_args()
    try:
        main(calibrate=args.calibrate)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        raise SystemExit(f"Serial connection failed on {PORT}: {exc}") from exc
