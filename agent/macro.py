"""
Match restart macro for RoA.

Menu navigation is inherently fragile if driven by fixed sleep() timings
(a slightly slower animation, a network hiccup, an unexpected dialog, all
break a pure-timing script). Wherever possible, this macro waits on
observable GAME STATE instead of guessing how long a menu takes -- e.g.
"wait until both players are back to full stock" is a much stronger
signal that a match has actually started than "wait 3 seconds and hope".

You WILL need to calibrate:
    - The exact number of "confirm"/"a" presses and their order, since
      that depends on RoA's menu flow, which I can't see from here.
    - Whether character/stage selection needs to be redone each time, or
      whether RoA remembers your last picks (common in "rematch" flows).

Recommended calibration approach:
    1. Manually play through: post-match screen(s) -> back to character
       select -> select Zetterburn, watch CPU pick Orcane -> select stage
       -> match starts. Count exactly how many screens/confirms this
       takes and note them below.
    2. Fill in MENU_STEPS to match what you observed.
    3. Run test_macro() standalone (bottom of this file) a few times to
       confirm it reliably gets you back into a fresh match before wiring
       it into roa_env.py.
"""

import time
from bridge import Bridge


# ----------------------------------------------------------------------
# Calibration knobs -- fill these in based on what you observe manually
# ----------------------------------------------------------------------

# Which joystick index sends menu confirms (usually your controlled player)
MENU_JOY_INDEX = 0

# Each step: (field, hold_seconds, wait_after_seconds)
# "a" is a guess for "confirm" -- swap to whatever RoA actually uses.
# wait_after_seconds is just a fallback pause between menu screens; keep
# these SHORT since the real gating happens via wait_for_match_start()
# below, not via these sleeps. They mainly exist to avoid double-inputs
# landing on the same screen before it's transitioned.
POST_MATCH_TO_CHAR_SELECT_STEPS = [
    ("a", 0.05, 1),   # e.g. dismiss results / victory screen
    ("a", 0.05, 1),   # e.g. dismiss a second results screen, if any
]

# How many times to tap "down" to move the cursor from its default
# position onto the "Ready to start" button, and how long to pause
# between taps so the menu doesn't miss/double-register a move.
CURSOR_DOWN_TAPS = 3          # CALIBRATE: how many presses actually needed
CURSOR_DOWN_TAP_HOLD = 0.05   # how long each tap is held
CURSOR_DOWN_TAP_GAP = 0.15    # pause between taps -- increase if taps get missed

CHARACTER_SELECT_STAGE_ID = 989

CHAR_SELECT_TO_MATCH_STEPS = [
    ("a", 0.05, 1),   # confirm character (assumes Zetterburn already
                         # highlighted/selected from a prior run -- verify!)
    ("a", 0.05, 1),   # confirm stage selection
]


class MatchMacro:
    def __init__(self, bridge: Bridge, joy_index: int = MENU_JOY_INDEX):
        self.bridge = bridge
        self.joy_index = joy_index

    def wait_for_character_select(self, timeout_seconds: float = 15.0,
                                   poll_interval: float = 0.2,
                                   required_consecutive: int = 3) -> bool:
        """
        Poll until game.stage == CHARACTER_SELECT_STAGE_ID, debounced the
        same way as wait_for_match_start, to confirm we've genuinely landed
        on the character select screen before sending any menu navigation
        inputs (rather than assuming a fixed number of post-match presses
        got us there).
        """
        deadline = time.time() + timeout_seconds
        consecutive_hits = 0

        while time.time() < deadline:
            state = self.bridge.get_state()
            stage = state.get("game", {}).get("stage")

            if stage == CHARACTER_SELECT_STAGE_ID:
                consecutive_hits += 1
                print(f"[MatchMacro] character select stage detected "
                      f"({consecutive_hits}/{required_consecutive})")
                if consecutive_hits >= required_consecutive:
                    print("[MatchMacro] character select screen confirmed.")
                    return True
            else:
                if consecutive_hits > 0:
                    print("[MatchMacro] stage check broke, resetting debounce count")
                consecutive_hits = 0

            time.sleep(poll_interval)

        print("[MatchMacro] TIMED OUT waiting for character select screen.")
        return False

    def _press(self, field: str, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(self.joy_index, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(self.joy_index, field, False)
        time.sleep(wait_after)

    def tap_direction(self, field: str, times: int, hold_seconds: float, gap_seconds: float):
        """
        Discrete directional taps for menu cursor movement (e.g. "ddown"
        to move a cursor down N items). Menus generally register one move
        per press EDGE, not per hold duration, so this taps on/off rather
        than holding continuously.
        """
        for _ in range(times):
            self.bridge.set_joy_button(self.joy_index, field, True)
            time.sleep(hold_seconds)
            self.bridge.set_joy_button(self.joy_index, field, False)
            time.sleep(gap_seconds)

    def _run_steps(self, steps):
        for field, hold_seconds, wait_after in steps:
            self._press(field, hold_seconds, wait_after)

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

    def restart_match(self, self_index: int = 0, opponent_index: int = 1) -> bool:
        """
        Full sequence: navigate from a just-ended match back into a fresh
        one. Returns True if a fresh match was confirmed to start, False
        if we timed out waiting (something in the menu flow likely needs
        recalibrating -- check MENU_STEPS above, or the game may be stuck
        on an unexpected screen).
        """
        self._run_steps(POST_MATCH_TO_CHAR_SELECT_STEPS)

        time.sleep(3)

        # if not self.wait_for_character_select():
        #     print("[MatchMacro] WARNING: never confirmed character select screen -- "
        #           "proceeding anyway, but menu navigation below may be landing on "
        #           "the wrong screen.")

        # Move cursor down onto "Ready to start" before confirming --
        # the cursor apparently always resets to the same spot above it.
        self.tap_direction("ddown", CURSOR_DOWN_TAPS, CURSOR_DOWN_TAP_HOLD, CURSOR_DOWN_TAP_GAP)

        time.sleep(0.5)

        self._run_steps(CHAR_SELECT_TO_MATCH_STEPS)

        return self.wait_for_match_start(self_index, opponent_index)


def test_macro():
    """
    Standalone test: manually get RoA into a just-finished-match state
    (i.e. sitting on the results/victory screen) before running this, then
    run this script directly to see if the macro correctly gets you back
    into a fresh match.
    """
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