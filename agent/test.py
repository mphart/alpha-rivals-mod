"""
Manual / interactive tests against an injected game instance.

Run while the game is on the local character select screen for the CSS
tests. Rebuild/reinject the DLL after the WritePlayerChoice / get_player_on
fixes before relying on those results.
"""

from __future__ import annotations

import json
import time
from bridge import Bridge


def _banner(title: str) -> None:
    print(f"\n=== {title} ===")


def _check(cond: bool, msg: str) -> bool:
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {msg}")
    return cond


def test_css_player_slots(bridge: Bridge, *, settle_s: float = 0.05) -> bool:
    """
    Comprehensive CSS test:
      - dump portraits
      - read/write player_on for all slots
      - read/write player_choice (old_char / CH_* id) including random=1
      - verify get_state reflects on/url while on CSS
      - verify out-of-range / missing-screen errors

    Requires local character select (cs_playerbg_obj instances present).
    """
    ok = True
    _banner("CSS dump")
    css = bridge.dump_css()
    boxes = css.get("boxes") or []
    print(f"  portraits: {len(boxes)}")
    for box in boxes:
        vars_ = {v.get("name"): v.get("v") for v in (box.get("vars") or []) if "v" in v}
        print(
            f"  id={box.get('id')} x={box.get('x')} y={box.get('y')} "
            f"player={vars_.get('player')} url={vars_.get('url')} "
            f"cursor_id={vars_.get('cursor_id')} draw_index={vars_.get('draw_index')}"
        )
    ok &= _check(len(boxes) >= 1, "at least one cs_playerbg_obj on screen")

    _banner("player_on round-trip")
    original_on = {}
    for p in range(4):
        try:
            original_on[p] = bridge.get_player_on(p)
            print(f"  slot {p} on={original_on[p]}")
        except Exception as e:
            ok &= _check(False, f"get_player_on {p}: {e}")
            original_on[p] = True

    # Flip each slot, verify, restore.
    for p in range(4):
        target = not original_on[p]
        written = bridge.set_player_on(p, target)
        time.sleep(settle_s)
        read_back = bridge.get_player_on(p)
        ok &= _check(
            (written != 0.0) == target and read_back == target,
            f"set_player_on {p} -> {target} (written={written}, read={read_back})",
        )
        bridge.set_player_on(p, original_on[p])

    # Explicit off for P3/P4 style use-case then restore.
    bridge.set_player_on(2, False)
    bridge.set_player_on(3, False)
    time.sleep(settle_s)
    ok &= _check(not bridge.get_player_on(2), "slot 2 off")
    ok &= _check(not bridge.get_player_on(3), "slot 3 off")
    for p, was_on in original_on.items():
        bridge.set_player_on(p, was_on)

    _banner("player_choice (url) round-trip")
    original_choice = {}
    for p in range(4):
        try:
            original_choice[p] = bridge.get_player_choice(p)
            print(f"  slot {p} url={original_choice[p]}")
        except RuntimeError as e:
            ok &= _check(False, f"get_player_choice {p}: {e}")
            return ok

    # Include url==0 — previously ambiguous with write-failure returning 0.
    test_urls = {0: 3, 1: 0, 2: 7, 3: 12}
    for p, url in test_urls.items():
        bridge.set_player_choice(p, url)
        time.sleep(settle_s)
        got = bridge.get_player_choice(p)
        ok &= _check(got == float(url), f"set_player_choice {p} -> {url} (read={got})")

    # Batch helper
    bridge.configure_css_slots(choices={0: 1, 1: 2}, on={0: True, 1: True})
    time.sleep(settle_s)
    ok &= _check(bridge.get_player_choice(0) == 1.0, "configure_css_slots choice 0")
    ok &= _check(bridge.get_player_choice(1) == 2.0, "configure_css_slots choice 1")
    ok &= _check(bridge.get_player_on(0), "configure_css_slots on 0")

    # Restore choices
    for p, url in original_choice.items():
        bridge.set_player_choice(p, int(url))

    _banner("get_state CSS fields")
    state = bridge.get_state()
    players = state.get("players") or []
    ok &= _check(len(players) == 4, f"get_state players len={len(players)}")
    for p, pl in enumerate(players):
        print(f"  state[{p}] on={pl.get('on')} url={pl.get('url')}")
        if p in original_on and original_on[p]:
            # After restore, on should match; url present when portrait exists.
            if "url" in pl:
                ok &= _check(
                    float(pl["url"]) == float(original_choice.get(p, pl["url"])),
                    f"get_state url matches restored choice for slot {p}",
                )

    _banner("error paths")
    try:
        bridge.set_player_on(9, True)
        ok &= _check(False, "set_player_on 9 should error")
    except RuntimeError as e:
        ok &= _check("0-3" in str(e) or "error" in str(e).lower(), f"set_player_on 9 -> {e}")

    try:
        bridge.set_player_choice(9, 1)
        ok &= _check(False, "set_player_choice 9 should error")
    except RuntimeError as e:
        ok &= _check("0-3" in str(e) or "error" in str(e).lower(), f"set_player_choice 9 -> {e}")

    _banner(f"CSS test {'PASSED' if ok else 'FAILED'}")
    return ok


def test_joy_claim(bridge: Bridge) -> None:
    _banner("joy claim smoke")
    for i in range(4):
        print(f"  override {i}:", bridge.set_joy_override(i, True))
    print("  press A on 0:", bridge.set_joy_button(0, "a", True))
    time.sleep(0.1)
    bridge.release_joy(0)
    for i in range(1, 4):
        bridge.release_joy(i)
    print("  released")


def main() -> None:
    bridge = Bridge()
    print("connected:", bridge.pipe_name)

    # Primary: character select configuration
    passed = test_css_player_slots(bridge)
    if not passed:
        raise SystemExit(1)

    # Optional pad smoke (does not fail the CSS suite)
    # test_joy_claim(bridge)


if __name__ == "__main__":
    #main()
    bridge = Bridge()

    instances = bridge.send("list_instances")
    print(json.dumps(json.loads(instances), indent=4))

    # vars = bridge.send("dump_vars 22")
    # print(vars)
    vars = bridge.send("dump_vars 40")
    print(vars)
    # if vars:
    #     print(json.dumps(["["+vars+"]"], indent=4))