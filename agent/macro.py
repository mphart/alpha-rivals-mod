"""
Match restart macro for RoA.
"""

import time
from bridge import Bridge

CHARACTER_SELECT_STAGE_ID = 989

class MatchMacro:
    def __init__(self, bridge: Bridge):
        self.bridge = bridge

    def _press(self, field: str, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(0, field, True)
        self.bridge.set_joy_button(1, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(0, field, False)
        self.bridge.set_joy_button(1, field, False)
        time.sleep(wait_after)

    def tap_direction(self, field: str, times: int, hold_seconds: float, gap_seconds: float):
        """
        Discrete directional taps for menu cursor movement (e.g. "ddown"
        to move a cursor down N items). Menus generally register one move
        per press EDGE, not per hold duration, so this taps on/off rather
        than holding continuously.
        """
        for _ in range(times):
            self.bridge.set_joy_button(0, field, True)
            self.bridge.set_joy_button(1, field, True)
            time.sleep(hold_seconds)
            self.bridge.set_joy_button(0, field, False)
            self.bridge.set_joy_button(1, field, False)
            time.sleep(gap_seconds)

    def wait_for_match_start(self, self_index: int, opponent_index: int,
                              expected_stock: float = 3.0,
                              timeout_seconds: float = 15.0,
                              poll_interval: float = 0.2,
                              required_consecutive: int = 3) -> bool:
        """
        Poll game state until both players are 'on' with full stock and
        (roughly) zero percent -- a much more reliable "match has started"
        signal than a fixed sleep.

        IMPORTANT: requires the condition to hold for `required_consecutive`
        consecutive polls before declaring success, not just once. Right as
        a match transitions (ending -> menu -> new match), the game's state
        can briefly show stale or transitional values that happen to look
        like "fresh match" for a single poll -- debouncing avoids treating
        that false positive as a real match start, which is what was
        letting actions get sent before the menu navigation had actually
        finished.

        Returns False on timeout.
        """
        deadline = time.time() + timeout_seconds
        consecutive_hits = 0

        while time.time() < deadline:
            state = self.bridge.get_state()
            players = state.get("players", [])

            is_fresh = False
            if len(players) > max(self_index, opponent_index):
                p_self = players[self_index]
                p_opp = players[opponent_index]

                stage_ok = state.get("game", {}).get("stage") != CHARACTER_SELECT_STAGE_ID

                is_fresh = (
                    stage_ok
                    and p_self.get("on") and p_opp.get("on")
                    and p_self.get("stock") == expected_stock
                    and p_opp.get("stock") == expected_stock
                    and p_self.get("percent", 0) < 5
                    and p_opp.get("percent", 0) < 5
                )

            if is_fresh:
                consecutive_hits += 1
                if consecutive_hits >= required_consecutive:
                    print("[MatchMacro] match start confirmed.")
                    return True
            else:
                if consecutive_hits > 0:
                    print("[MatchMacro] fresh-match check broke, resetting debounce count")
                consecutive_hits = 0

            time.sleep(poll_interval)

        print("[MatchMacro] TIMED OUT waiting for match start.")
        return False

    def _claim_menu_pads(self):
        self.bridge.set_joy_override(0, True)
        self.bridge.set_joy_override(1, True)

    def _release_menu_pads(self):
        self.bridge.release_joy(0)
        self.bridge.release_joy(1)

    def restart_match(self, self_index: int = 0, opponent_index: int = 1) -> bool:
        self._claim_menu_pads()
        try:
            # post-match screen
            self._press("a", 0.05, 1)
            self._press("a", 0.05, 1)
            time.sleep(3)

            # character select screen
            self.tap_direction("ddown", 3, 0.05, 0.15)
            time.sleep(0.5)

            # map select screen
            self._press("a", 0.05, 1)
            self.tap_direction("dup", 6, 0.05, 0.15)
            time.sleep(0.5)
            self._press("a", 0.05, 1)
            time.sleep(0.5)

            # wait for match
            return self.wait_for_match_start(self_index, opponent_index)
        finally:
            self._release_menu_pads()

    def quit_match(self) -> bool:
        self._claim_menu_pads()
        try:
            self._press("start", 0.05, 1)
            self.tap_direction("ddown", 3, 0.05, 0.15)
        finally:
            self._release_menu_pads()


def test_macro():
    bridge = Bridge()
    macro = MatchMacro(bridge)

    print("Attempting to restart match...")
    success = macro.restart_match(self_index=0, opponent_index=1)

    if success:
        print("Match restarted successfully -- state confirms full stock, low percent.")
    else:
        print("Timed out waiting for match start. Check MENU_STEPS timing/order,"
              " or increase timeout_seconds if RoA's menus are just slow.")


if __name__ == "__main__":
    test_macro()