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
    # Auto-discovers and claims two DIFFERENT available instances (assuming
    # two games are running with the DLL injected) -- the second Bridge()
    # call will skip whichever pipe the first one already claimed, since
    # that pipe is now busy from the OS's perspective.
    bridge1 = Bridge()
    print("bridge1 connected to:", bridge1.pipe_name)
    print(bridge1.get_state())

    bridge2 = Bridge()
    print("bridge2 connected to:", bridge2.pipe_name)
    print(bridge2.get_state())