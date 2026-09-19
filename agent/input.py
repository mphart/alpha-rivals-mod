import numpy as np
from gymnasium import spaces
import time

from bridge import Bridge

BUTTON_FIELDS = ["a", "b", "x", "y", "lb", "dup"] 

STICK_FIELDS = ["lx", "ly"]
STICK_MAX = 32767

NODES_PER_STICK = 51
NODES_PER_BUTTON = 2

# 51 evenly spaced axis values: -32767 ... 0 ... 32767
STICK_VALUES = np.clip(
    np.round(np.linspace(-STICK_MAX, STICK_MAX, NODES_PER_STICK)),
    -STICK_MAX,
    STICK_MAX,
).astype(np.int32)

class InputManager:
    def __init__(self, bridge: Bridge):
        self.bridge = bridge

        # MultiDiscrete: 6 buttons (off/on) + 2 stick axes (51 bins each)
        self.action_space = spaces.MultiDiscrete(
            [NODES_PER_BUTTON] * len(BUTTON_FIELDS)
            + [NODES_PER_STICK] * len(STICK_FIELDS)
        )

        # keep track of the previous button states
        # this saves us time by not sending the same button state multiple 
        # frames in a row
        self.prev_buttons_held = {
            0: {f: False for f in BUTTON_FIELDS},
            1: {f: False for f in BUTTON_FIELDS},
            2: {f: False for f in BUTTON_FIELDS},
            3: {f: False for f in BUTTON_FIELDS},
        }
    

    def get_action_space(self):
        return self.action_space

    def apply_action(self, action, player_index):
        action = np.asarray(action).reshape(-1)
        num_buttons = len(BUTTON_FIELDS)
        prev = self.prev_buttons_held[player_index]

        for i, field in enumerate(BUTTON_FIELDS):
            down = bool(int(action[i]) != 0)
            if down != prev[field]:
                self.bridge.set_joy_button(player_index, field, down)
                prev[field] = down

        for j, field in enumerate(STICK_FIELDS):
            idx = int(np.clip(int(action[num_buttons + j]), 0, NODES_PER_STICK - 1))
            value = int(round(STICK_VALUES[idx]))
            value = int(np.clip(value, -STICK_MAX, STICK_MAX))
            self.bridge.set_joy_axis(player_index, field, value)
    
    # claim menu pads for the given indexes, or all 4 of them by default
    def claim_menu_pads(self, indexes: list[bool] = [True, True, True, True]):
        for i, index in enumerate(indexes):
            if index:
                self.bridge.set_joy_override(i, True)

    # release all menu pads
    def release_menu_pads(self):
        for player_index in range(4):
            prev = self.prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                prev[field] = False
            self.bridge.release_joy(player_index)

    # tap a direction on the menu cursor
    def tap_direction(self, field: str, joy_index: int, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(joy_index, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(joy_index, field, False)
        time.sleep(wait_after)

    # press a button
    def press(self, field: str, joy_index: int, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(joy_index, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(joy_index, field, False)
        time.sleep(wait_after)

    # press a button for all indexes that are True
    def press_all(self, indexes: list[bool], field: str, hold_seconds: float, wait_after: float):
        for i, index in enumerate(indexes):
            if index:
                self.press(field, i, hold_seconds, wait_after)

    def reset_inputs(self, player_index: int):
        prev = self.prev_buttons_held[player_index]
        for field in BUTTON_FIELDS:
            self.bridge.set_joy_button(player_index, field, False)
            prev[field] = False
        for field in STICK_FIELDS:
            self.bridge.set_joy_axis(player_index, field, 0)
    
    # reset the input state for all players
    # both joystick axes set to 0 and all buttons released
    def reset_all_inputs(self):
        for player_index in range(4):
            prev = self.prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                self.bridge.set_joy_button(player_index, field, False)
                prev[field] = False
            for field in STICK_FIELDS:
                self.bridge.set_joy_axis(player_index, field, 0)
