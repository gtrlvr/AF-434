# Keyboard -> Arduino serial bridge for the iCL42-08
# Hold A = counter-clockwise, hold D = clockwise (release = stop)
# Q = speed up, E = speed down, Esc = quit
#
# Install deps:  pip install pyserial pynput

import time
import serial
from pynput import keyboard

PORT = "COM6"          # Windows: "COM3" etc. | Linux: "/dev/ttyACM0" or "/dev/ttyUSB0"
BAUD = 115200

ser = serial.Serial(PORT, BAUD)
time.sleep(2)          # let the Arduino reset after opening the port

held = set()

def send(cmd: str):
    ser.write(cmd.encode())

def on_press(key):
    try:
        k = key.char.lower()
    except AttributeError:
        return
    if k in held:
        return          # ignore key auto-repeat
    held.add(k)
    if k == "a":
        send("a")
        print("CCW")
    elif k == "d":
        send("d")
        print("CW")
    elif k == "q":
        send("q")
        print("Speed +")
    elif k == "e":
        send("e")
        print("Speed -")

def on_release(key):
    if key == keyboard.Key.esc:
        send("s")
        ser.close()
        print("Exiting.")
        return False    # stops the listener
    try:
        k = key.char.lower()
    except AttributeError:
        return
    held.discard(k)
    if k in ("a", "d"):
        # If the other direction key is still held, keep moving that way
        if "a" in held:
            send("a")
        elif "d" in held:
            send("d")
        else:
            send("s")
            print("Stop")

print("A = CCW | D = CW | Q/E = speed up/down | Esc = quit")
with keyboard.Listener(on_press=on_press, on_release=on_release) as listener:
    listener.join()