"""
Bridge client for talking to an injected RoA payload over the named pipe.
"""

import json
import os
import re
import time
import win32file
import win32pipe
import pywintypes


PIPE_DIR = r'\\.\pipe'
PIPE_PREFIX = 'bridge_'
PIPE_NAME_RE = re.compile(r'^bridge_\d+$')

ERROR_PIPE_BUSY = 231
CLAIM_ATTEMPTS = 40
CLAIM_DELAY_SECONDS = 0.25


def _connect_pipe(full_path: str):
    handle = win32file.CreateFile(
        full_path,
        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
        0, None, win32file.OPEN_EXISTING, 0, None
    )
    win32pipe.SetNamedPipeHandleState(handle, win32pipe.PIPE_READMODE_MESSAGE, None, None)
    return handle


def _find_and_claim_pipe() -> tuple[str, object]:
    """
    Enumerate named pipes matching bridge_<pid>, and attempt to connect to
    each in turn, claiming the first one that isn't already in use by
    another Bridge instance. Connecting is attempted immediately per
    candidate (rather than listing first, connecting separately) to avoid
    a race where another process claims a pipe between listing and
    connecting.
    """
    last_error = None
    last_candidate_count = 0
    for attempt in range(CLAIM_ATTEMPTS):
        try:
            names = os.listdir(PIPE_DIR)
        except OSError as e:
            last_error = e
            time.sleep(CLAIM_DELAY_SECONDS)
            continue

        candidates = [n for n in names if PIPE_NAME_RE.match(n)]
        last_candidate_count = len(candidates)
        for name in candidates:
            full_path = f"{PIPE_DIR}\\{name}"
            try:
                return full_path, _connect_pipe(full_path)
            except pywintypes.error as e:
                last_error = e
                # ERROR_PIPE_BUSY (231): another Bridge already claimed this
                # instance (nMaxInstances=1 server-side). Try the next candidate.
                continue

        time.sleep(CLAIM_DELAY_SECONDS)

    if last_candidate_count == 0:
        raise RuntimeError(
            f"No named pipes found matching '{PIPE_PREFIX}<pid>' under {PIPE_DIR}. "
            "Is the DLL injected into at least one running game instance?"
        )
    raise RuntimeError(
        f"Found {last_candidate_count} candidate pipe(s) matching '{PIPE_PREFIX}<pid>', "
        f"but could not connect to any of them (all busy, or another error). "
        f"Launch at least as many injected game instances as N_ENVS. "
        f"Last error: {last_error}"
    )


class Bridge:
    def __init__(self, pipe_name: str | None = None, pid: int | None = None):
        """
        Three ways to connect:
          - Bridge(pid=1234)          -> connect to a specific known instance
          - Bridge(pipe_name=r'...')  -> connect to an exact pipe path
          - Bridge()                  -> auto-discover and claim the first
                                         available bridge_<pid> pipe
        """
        if pid is not None:
            full_path = f"{PIPE_DIR}\\{PIPE_PREFIX}{pid}"
            self.pipe = _connect_pipe(full_path)
            self.pipe_name = full_path
        elif pipe_name is not None:
            self.pipe = _connect_pipe(pipe_name)
            self.pipe_name = pipe_name
        else:
            self.pipe_name, self.pipe = _find_and_claim_pipe()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self):
        pipe = getattr(self, "pipe", None)
        if pipe:
            try:
                win32file.CloseHandle(pipe)
            finally:
                self.pipe = None

    def send(self, command: str) -> str:
        win32file.WriteFile(self.pipe, command.encode())
        result = win32file.ReadFile(self.pipe, 262144)
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

    def set_player_stocks(self, player: int, stocks: int) -> float:
        """
        Write stock count for slot player (0-3).
        Uses existing ``set_player_stock`` / WritePlayerStock. Returns the value written.
        """
        raw = self._require_ok(
            self.send(f"set_player_stock {player} {int(stocks)}"),
            "set_player_stock",
        )
        return float(raw)

    def get_game_stage(self) -> float:
        """Read the current stage id (ReadGameStage)."""
        return float(self.send("get_game_stage"))

    def get_game_is_running(self) -> bool:
        """
        Whether a match appears to be in progress.

        RoA has no durable GML bool named ``game_is_running``. The engine's own
        check is the script ``is_gameplay_room()`` (room-id OR list). This
        wrapper instead asks the DLL for a memory proxy:

        - ``True`` if the current room has a live ``gameplay_parent`` instance
          (match controller) **or** at least one live ``oPlayer``.
        - ``False`` on CSS, stage select, results, and other menus (no fighters /
          match controller in the room list).

        Useful as a fast "are we in a fight?" poll; still debounce around room
        transitions if you need menu-navigation safety.
        """
        return self.send("get_game_is_running").strip() != "0"

    def get_is_map_selection(self) -> bool:
        """
        Whether the stage/map select screen appears to be up.

        Character select and map select share the same ``game_stage`` id, so
        stage alone cannot tell them apart. There is also no dedicated GML
        ``is_map_selection`` bool. This wrapper uses a room-instance proxy:

        - ``True`` if a live ``ss_stagebox_obj`` or ``ss_stage_header_obj``
          exists (stage-select UI; room ``local_stage_select`` /
          ``network_stage_select``).
        - ``False`` on character select (``cs_playerbg_obj`` instead), in
          matches, results, and other menus.
        """
        return self.send("get_is_map_selection").strip() != "0"

    def get_is_post_match(self) -> bool:
        """
        Whether the post-match results screen appears to be up.

        ``game_stage`` is not a stable id for this screen (it is not always
        1039). There is no dedicated GML bool either. This wrapper uses a
        room-instance proxy:

        - ``True`` if a live ``draw_result_screen`` or ``result_screen_box``
          exists (versus results), or ``chapter_results_object`` (story).
        - ``False`` on CSS, stage select, in-match HUD, and other menus.
        """
        return self.send("get_is_post_match").strip() != "0"

    def get_gameplay_time(self) -> float:
        """
        Frames since the match room started, including the 3-2-1-GO countdown.

        Same value as GML ``get_gameplay_time()`` (global ``gameplay_time``).
        """
        return float(self.send("get_gameplay_time"))

    def get_countdown(self) -> float:
        """
        Estimated frames left on the match-start 3-2-1-GO countdown.

        Derived from ``gameplay_time`` (30 frames per beat, GO at 120).
        Returns 0 when not in a match or after GO. Prefer
        ``get_can_make_inputs`` to know when fight inputs are actually live.
        """
        return float(self.send("get_countdown"))

    def get_can_make_inputs(self) -> bool:
        """
        Whether the running match is accepting fight inputs.

        There is no durable GML ``can_make_inputs`` bool. During countdown
        every ``oPlayer`` is locked in ``PS_SPAWN`` (24) and attack/special
        processing is skipped. This wrapper is true when:

        - a match is in progress (``gameplay_parent`` / ``oPlayer`` present)
        - results are not up
        - global ``gameplay_has_stopped`` / ``game_ending`` are not set
        - at least one live ``oPlayer`` has left ``PS_SPAWN``
        """
        return self.send("get_can_make_inputs").strip() != "0"

    def dump_css(self) -> dict:
        """Dump cs_playerbg_obj instances (object 219) and their custom vars."""
        return json.loads(self.send("dump_css"))

    # ---- Character select / player slots ----
    @staticmethod
    def _require_ok(response: str, action: str) -> str:
        if response.startswith("error:"):
            raise RuntimeError(f"{action} failed: {response}")
        return response

    def get_player_on(self, player: int) -> bool:
        """Read whether CSS/global slot player (0-3) is marked on."""
        raw = self._require_ok(self.send(f"get_player_on {player}"), "get_player_on")
        return float(raw) != 0.0

    def get_player_cursor_y(self, player: int) -> float:
        """Read CSS cursor Y for slot player (0-3) via ReadPlayerCursorY."""
        raw = self._require_ok(
            self.send(f"get_player_cursor_y {player}"),
            "get_player_cursor_y",
        )
        return float(raw)

    def set_player_on(self, player: int, on: bool) -> float:
        """
        Write the durable player_on flag for slot player (0-3).
        Returns the value written (0.0 or 1.0).
        """
        raw = self._require_ok(
            self.send(f"set_player_on {player} {1 if on else 0}"),
            "set_player_on",
        )
        return float(raw)

    def get_player_choice(self, player: int) -> float:
        """
        Read CSS character pick for slot player (0-3).
        Values match CH_* ids (1=random, 2=Zetterburn, 3=Orcane, ...).
        Backed by the CE player array parallel to cursor_y (not old_char).
        """
        raw = self._require_ok(
            self.send(f"get_player_choice {player}"),
            "get_player_choice",
        )
        return float(raw)

    def set_player_choice(self, player: int, url: int) -> str:
        """
        Write CSS character pick for slot player (0-3).
        Returns the pipe response (``ok`` on success).
        """
        return self._require_ok(
            self.send(f"set_player_choice {player} {int(url)}"),
            "set_player_choice",
        )

    def configure_css_slots(
        self,
        choices: dict[int, int] | None = None,
        on: dict[int, bool] | None = None,
    ) -> None:
        """
        Batch-configure CSS: set on/off flags and/or character urls.
        Example: configure_css_slots(choices={0: 3, 1: 5}, on={2: False, 3: False})
        """
        if on:
            for player, enabled in on.items():
                self.set_player_on(player, enabled)
        if choices:
            for player, url in choices.items():
                self.set_player_choice(player, url)

    # ---- Joystick input ----
    def set_joy_button(self, joy_index: int, button: str, down: bool) -> str:
        return self.send(f"set_joy {joy_index} {button} {1 if down else 0}")

    def set_joy_axis(self, joy_index: int, axis: str, value: int) -> str:
        # value range depends on what the calibration log shows -- 0-65535
        # with 32767 as center is the typical Windows joystick convention.
        return self.send(f"set_joy {joy_index} {axis} {value}")

    def set_joy_override(self, joy_index: int, enabled: bool) -> str:
        return self.send(f"set_joy_override {joy_index} {1 if enabled else 0}")

    def release_all(self, joy_index: int) -> None:
        for button in ("a", "b", "x", "y", "lb", "dup"):
            self.set_joy_button(joy_index, button, False)
        self.set_joy_axis(joy_index, "lx", 0)
        self.set_joy_axis(joy_index, "ly", 0)

    def release_joy(self, joy_index: int) -> None:
        self.release_all(joy_index)
        self.set_joy_override(joy_index, False)


if __name__ == "__main__":
    bridge1 = Bridge()



    print(bridge1.get_state())
    # print("bridge1 connected to:", bridge1.pipe_name)
    # print(bridge1.set_player_choice(0, 3))
    # print("readback:", bridge1.get_player_choice(0))
    # print("game stage:", bridge1.get_game_stage())

    # print(bridge1.set_joy_button(0, "a", True))
    # print(bridge1.set_joy_button(1, "a", True))
    # print(bridge1.set_joy_button(2, "a", True))
    # print(bridge1.set_joy_button(3, "a", True))
    # sleep(0.1)
    # print(bridge1.set_joy_button(0, "a", False))
    # print(bridge1.set_joy_button(1, "a", False))
    # print(bridge1.set_joy_button(2, "a", False))
    # print(bridge1.set_joy_button(3, "a", False))

    # set_joy_button(0, "start", True)

    # print(bridge1.set_joy_button(0, "a", False))

    # print("stage: ", bridge1.get_game_stage())
    # print("game is running: ", bridge1.get_game_is_running())
    # print("cursor y: ", bridge1.get_player_cursor_y(0))
    # print("is map selection: ", bridge1.get_is_map_selection())