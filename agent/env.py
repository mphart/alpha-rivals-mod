"""
Gymnasium environment for Rivals of Aether, driven through the injected
DLL bridge (agent_bridge.Bridge).

Reward design (intentionally minimal, per experiment):
    - Small constant reward every step, just for surviving/existing.
    - A reward whenever the OPPONENT's stock count decreases.
    - Nothing else -- no percent shaping, no position shaping, no
      distance-to-opponent shaping. This is deliberately sparse to see
      whether PPO can bootstrap useful behavior from stock loss alone.

NOTE: match_reset() is currently a stub. You'll need a macro (this could
be as simple as more `set_key`/`set_joy` calls to navigate menus, or a
separate "restart match" address you find via Cheat Engine) to actually
start a new match when reset() is called. Until that exists, reset()
just re-reads the current state and assumes a match is already running.
"""

import time
import numpy as np
import gymnasium as gym
from gymnasium import spaces

from bridge import Bridge
from macro import MatchMacro
from reward import RewardManager
from reset import ResetManager


BUTTON_FIELDS = ["a", "b", "x", "y", "lb", "dup"] 
STICK_FIELDS = ["lx", "ly"]
STICK_MAX = 32767

FPS = 30


class RoAEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(
        self,
        controlled_joy: int = 0, # TODO make the controlled joystick changeable
        self_player_index: int = 0,
        opponent_player_index: int = 1,
        step_duration: float = 1.0 / FPS,  # how long an action is held, seconds (game is 60 fps, so 1/30 is 30 fps)
        max_episode_steps: int = 100 * 60 * FPS,   # 100 minutes of game time at 60 steps/sec
    ):
        super().__init__()

        self.bridge = Bridge()
        self.reward_manager = RewardManager()
        self.reset_manager = ResetManager(self.bridge)

        self.controlled_joy = controlled_joy
        self.self_player_index = self_player_index
        self.opponent_player_index = opponent_player_index
        self.step_duration = step_duration
        self.max_episode_steps = max_episode_steps
        self.prev_state = None

        # ---- Action space ----
        # Discrete: one entry per button (0=up,1=down)
        # Continuous: one entry per stick axis (value between -1.0 and 1.0)
        self.action_space = spaces.Box(
            low=np.concatenate([
                np.zeros(len(BUTTON_FIELDS), dtype=np.float32),
                np.full(len(STICK_FIELDS), -1.0, dtype=np.float32),
            ]),
            high=np.concatenate([
                np.ones(len(BUTTON_FIELDS), dtype=np.float32),
                np.ones(len(STICK_FIELDS), dtype=np.float32),
            ]),
            dtype=np.float32,
        )

        # ---- Observation space ----
        # Flattened from bridge.get_state()
        self.num_player_slots = 4
        self.values_per_player = 17
        self.num_game_values = 3
        obs_dim = self.num_player_slots * self.values_per_player + self.num_game_values
        self.observation_space = spaces.Box(
            low=-1e6, high=1e6, shape=(obs_dim,), dtype=np.float32
        )

        self._steps_this_episode = 0
        self._prev_buttons_held = {f: False for f in BUTTON_FIELDS}

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        self._release_all_buttons()
        self.reset_manager.reset()

        state = self.bridge.get_state()
        obs = self._state_to_obs(state)
        self.prev_state = state

        self._steps_this_episode = 0

        info = {}
        return obs, info

    def step(self, action):
        self._apply_action(action)

        # Hold the action for a fixed slice of real time. This is a real
        # game running in real time, not a turn-based simulator, so we
        # can't "step" it directly -- we just wait.
        # in the future, we will make this better
        time.sleep(self.step_duration)

        curr_state = self.bridge.get_state()
        obs = self._state_to_obs(curr_state)

        reward, terminated = self.reward_manager.compute_reward(self.prev_state, curr_state, self.self_player_index, self.opponent_player_index)
        self.prev_state = curr_state

        # check if we reached max episode steps
        # for now, we don't want any truncation
        # the end of the match will always end the episode
            # self._steps_this_episode += 1
            # truncated = self._steps_this_episode >= self.max_episode_steps
        truncated = False

        info = {}
        return obs, reward, terminated, truncated, info

    def close(self):
        self._release_all_buttons()

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _apply_action(self, action):
        action = np.asarray(action)
        num_buttons = len(BUTTON_FIELDS)

        for i, field in enumerate(BUTTON_FIELDS):
            down = bool(action[i] >= 0.5)
            if down != self._prev_buttons_held[field]:
                self.bridge.set_joy_button(self.controlled_joy, field, down)
                self._prev_buttons_held[field] = down

        for j, field in enumerate(STICK_FIELDS):
            raw = float(np.clip(action[num_buttons + j], -1.0, 1.0))
            value = int(round(raw * STICK_MAX))
            self.bridge.set_joy_axis(self.controlled_joy, field, value)

    def _release_all_buttons(self):
        for field in BUTTON_FIELDS:
            self.bridge.set_joy_button(self.controlled_joy, field, False)
            self._prev_buttons_held[field] = False
        for field in STICK_FIELDS:
            self.bridge.set_joy_axis(self.controlled_joy, field, 0)

    def _state_to_obs(self, state: dict) -> np.ndarray:
        values = []
        players = state.get("players", [])

        for i in range(self.num_player_slots):
            if i < len(players) and players[i].get("on", False):
                p = players[i]
                values.extend([
                    1.0, # on
                    float(p.get("percent", 0.0)),
                    float(p.get("stock", 0.0)),
                    float(p.get("x", 0.0)),
                    float(p.get("y", 0.0)),
                    float(p.get("vel_x", 0.0)),
                    float(p.get("vel_y", 0.0)),
                    float(p.get("anim", 0.0)),
                    float(p.get("anim_sprite", 0.0)),
                    float(p.get("frames_left", 0.0)),
                    float(p.get("character", 0.0)),
                    float(p.get("team", 0.0)),
                    float(p.get("used_air_dodge", 0.0)),
                    float(p.get("jumps_left", 0.0)),
                    float(p.get("on_fire", 0.0)),
                    float(p.get("dir", 0.0)),
                    float(p.get("invuln", 0.0)),
                ])
            else:
                values.extend([0.0] * self.values_per_player)

        game = state.get("game", {})
        stage = game.get("stage", 938) - 939 # stage id of the first stage
        clock = game.get("clock", 0.0)
        teams_enabled = game.get("teams_enabled", 0.0)
        values.extend([
            float(stage),
            float(clock),
            float(teams_enabled),
        ])

        return np.array(values, dtype=np.float32)
