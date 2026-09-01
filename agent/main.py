"""
Bridge client for talking to the injected RoA payload over the named pipe.

Usage examples are in __main__ at the bottom -- run this file directly for a
quick manual test, or import `Bridge` from your training script.
"""

import json
import time
import win32file


class Bridge:
    def __init__(self, pipe_name: str = r'\\.\pipe\bridge'):
        self.pipe = win32file.CreateFile(
            pipe_name,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0, None, win32file.OPEN_EXISTING, 0, None
        )

    def send(self, command: str) -> str:
        win32file.WriteFile(self.pipe, command.encode())
        result = win32file.ReadFile(self.pipe, 4096)
        return result[1].decode()

    # ---- State ----
    def get_state(self) -> dict:
        raw = self.send("get_state")
        return json.loads(raw)

    def get_percent(self) -> list[float]:
        raw = self.send("get_percent")
        return [float(x) for x in raw.split()]

    def get_stock(self) -> list[float]:
        raw = self.send("get_stock")
        return [float(x) for x in raw.split()]

    # ---- Keyboard input ----
    def set_key(self, vkey: int, down: bool) -> str:
        return self.send(f"set_key {vkey:x} {1 if down else 0}")

    # ---- Joystick input ----
    def set_joy_button(self, joy_index: int, button: str, down: bool) -> str:
        return self.send(f"set_joy {joy_index} {button} {1 if down else 0}")

    def set_joy_axis(self, joy_index: int, axis: str, value: int) -> str:
        # value range depends on what the calibration log shows -- 0-65535
        # with 32767 as center is the typical Windows joystick convention.
        return self.send(f"set_joy {joy_index} {axis} {value}")

    def release_all(self, joy_index: int) -> None:
        for button in ("attack", "jump", "special", "shield", "strong"):
            self.set_joy_button(joy_index, button, False)
        self.set_joy_axis(joy_index, "x", 32767)
        self.set_joy_axis(joy_index, "y", 32767)


if __name__ == "__main__":
    bridge = Bridge()

    # Quick sanity check
    print("state:", bridge.get_state())

    # Player 0 (joystick index 0): press attack, hold briefly, release
    print(bridge.set_joy_button(0, "attack", True))
    time.sleep(0.2)
    print(bridge.set_joy_button(0, "attack", False))

    # Player 0: move right
    print(bridge.set_joy_axis(0, "x", 65535))
    time.sleep(0.5)
    print(bridge.set_joy_axis(0, "x", 32767))  # back to center

    # Player 1 (joystick index 1), independent of player 0
    print(bridge.set_joy_button(1, "jump", True))
    time.sleep(0.2)
    print(bridge.set_joy_button(1, "jump", False))