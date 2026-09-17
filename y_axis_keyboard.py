"""Single-motor Y-axis keyboard control for an Arduino Mega 2560.

Upload y_axis_control.txt as an Arduino sketch first.
Motor wiring: D9 -> PUL+, D8 -> DIR+, GND -> PUL- and DIR-.
Hold Up/Down arrows to move; release to stop.
Q/E = speed up/down, Esc = stop and quit.
Run: python y_axis_keyboard.py --port COM9
Install dependencies: python -m pip install -r requirements.txt
"""

import argparse
import threading
import time

import serial
from pynput import keyboard

PORT = "COM9"
BAUD = 115200
EXPECTED_FIRMWARE = "AF434-Y-AXIS-v2"
HEARTBEAT_INTERVAL_S = 0.1


class KeyboardController:
    def __init__(self, connection):
        self.connection = connection
        self.held = []  # Most recently pressed direction wins.
        self.lock = threading.Lock()
        self.exiting = False

    @staticmethod
    def command_for_key(key):
        if key == keyboard.Key.up:
            return "u"
        if key == keyboard.Key.down:
            return "j"
        char = getattr(key, "char", None)
        if char and char.lower() in ("q", "e"):
            return char.lower()
        return None

    def send(self, command):
        # Call with the lock held to serialize key events and heartbeats.
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
        command = self.command_for_key(key)
        if command is None:
            return
        with self.lock:
            if self.exiting or command in self.held:
                return  # Ignore keyboard auto-repeat.
            self.held.append(command)
            self.send(command)
            print({"u": "Y-axis up command sent",
                   "j": "Y-axis down command sent",
                   "q": "Y-axis speed +",
                   "e": "Y-axis speed -"}[command])

    def on_release(self, key):
        command = self.command_for_key(key)
        if command is None:
            return
        with self.lock:
            if self.exiting or command not in self.held:
                return
            self.held.remove(command)
            if command in ("u", "j"):
                direction = next((c for c in reversed(self.held)
                                  if c in ("u", "j")), "s")
                self.send(direction)
                if direction == "s":
                    print("Y-axis stopped")


def print_arduino_messages(connection, pending):
    pending += connection.read(connection.in_waiting)
    lines = pending.split(b"\n")
    for line in lines[:-1]:
        print(f"Arduino: {line.decode('ascii', errors='replace').strip()}")
    return lines[-1]


def main(port=PORT):
    with serial.Serial(port, BAUD, timeout=1, write_timeout=1) as connection:
        controller = KeyboardController(connection)
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
                    f"Expected Y-axis firmware {EXPECTED_FIRMWARE}; received "
                    f"{firmware or 'no response'}. Upload y_axis_control.txt "
                    "as an Arduino sketch to the Mega, then restart Python."
                )
            print(f"Arduino Y-axis firmware verified: {firmware}")
            print("Up/Down arrows = move | Release = stop | Q/E = speed +/- | Esc = quit")
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
                pass  # Firmware's 500 ms timeout handles a lost USB link.
            print("Y-axis stopped. Exiting.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=PORT, help="Mega serial port (default: COM9).")
    args = parser.parse_args()
    try:
        main(port=args.port)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        raise SystemExit(f"Serial connection failed on {args.port}: {exc}") from exc
