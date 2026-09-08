"""
Gymnasium environment for Rivals of Aether, driven through the injected
DLL bridge (agent_bridge.Bridge).

Self play
"""

import re
import time
from pathlib import Path

import numpy as np
import gymnasium as gym
from gymnasium import spaces
from stable_baselines3 import PPO

from bridge import Bridge
from reward import RewardManager
from reset import ResetManager

AGENT_DIR = Path(__file__).resolve().parent
CHECKPOINT_DIR = AGENT_DIR / "checkpoints"
CHECKPOINT_STEPS_RE = re.compile(r"_(\d+)_steps\.zip$", re.IGNORECASE)

BUTTON_FIELDS = ["a", "b", "x", "y", "lb", "dup"] 
STICK_FIELDS = ["lx", "ly"]
STICK_MAX = 32767

FPS = 30


class RoAEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(
        self,
        training_steps: int = 10_000,
        self_player_index: int = 0,
        opponent_player_index: int = 1,
        step_duration: float = 1.0 / FPS,  # how long an action is held, seconds (game is 60 fps, so 1/30 is 30 fps)
        max_episode_steps: int = 100 * 60 * FPS,   # 100 minutes of game time at FPS steps/sec
        step_offset: int = 25_000,  # checkpoint save interval; used to weight opponent sampling
    ):
        super().__init__()

        self.bridge = Bridge()
        self.reward_manager = RewardManager()
        self.reset_manager = ResetManager(self.bridge)

        self.training_steps = training_steps
        self.self_player_index = self_player_index
        self.opponent_player_index = opponent_player_index
        self.step_duration = step_duration
        self.max_episode_steps = max_episode_steps
        self.step_offset = step_offset

        self.prev_state = None

        # ---- Action space ----
        # Continuous: one entry per button, make sure to round to 0 (up) or 1 (down)
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
        self.num_game_values = 4
        obs_dim = self.num_player_slots * self.values_per_player + self.num_game_values
        self.observation_space = spaces.Box(
            low=-1e6, high=1e6, shape=(obs_dim,), dtype=np.float32
        )

        self._steps_this_episode = 0
        self._prev_buttons_held = {
            0: {f: False for f in BUTTON_FIELDS},
            1: {f: False for f in BUTTON_FIELDS},
        }

        # Weighted-random compatible checkpoint, or None (idle opponent).
        self.opponent_model = self._load_static_opponent(self.step_offset)

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        # Randomly pick which slot the learner controls this episode.
        # Use the env RNG so seeding actually affects the coin flip.
        self.self_player_index = int(self.np_random.integers(0, 2))
        self.opponent_player_index = 1 - self.self_player_index

        self._release_all_buttons()
        self.reset_manager.reset()

        # get the initial state
        state = self.bridge.get_state()
        obs = self._state_to_obs(state, self.self_player_index)
        self.prev_state = state
        self._steps_this_episode = 0

        # try to load a new opponent model
        self.opponent_model = self._load_static_opponent(self.step_offset)

        info = {}
        return obs, info

    def step(self, action):
        # model
        self._apply_action(action, self.self_player_index)

        # opponent (frozen snapshot; idle if no compatible checkpoint)
        if self.opponent_model is not None:
            obs_opponent = self._state_to_obs(self.prev_state, self.opponent_player_index)
            action_opponent, _ = self.opponent_model.predict(
                obs_opponent, deterministic=False
            )
            self._apply_action(action_opponent, self.opponent_player_index)

        # Hold the action for a fixed slice of real time
        time.sleep(self.step_duration)

        # get the new state
        curr_state = self.bridge.get_state()
        obs = self._state_to_obs(curr_state, self.self_player_index)

        # compute the reward
        reward, terminated = self.reward_manager.compute_reward(self.prev_state, curr_state, self.self_player_index, self.opponent_player_index)
        self.prev_state = curr_state

        truncated = False
        info = {}
        return obs, reward, terminated, truncated, info

    def close(self):
        self._release_all_buttons()

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _apply_action(self, action, player_index):
        action = np.asarray(action, dtype=np.float32).reshape(-1)
        num_buttons = len(BUTTON_FIELDS)
        prev = self._prev_buttons_held[player_index]

        for i, field in enumerate(BUTTON_FIELDS):
            down = bool(action[i] >= 0.5)
            if down != prev[field]:
                self.bridge.set_joy_button(player_index, field, down)
                prev[field] = down

        for j, field in enumerate(STICK_FIELDS):
            raw = float(np.clip(action[num_buttons + j], -1.0, 1.0))
            value = int(round(raw * STICK_MAX))
            self.bridge.set_joy_axis(player_index, field, value)

    def _release_all_buttons(self):
        for player_index in (0, 1):
            prev = self._prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                self.bridge.set_joy_button(player_index, field, False)
                prev[field] = False
            for field in STICK_FIELDS:
                self.bridge.set_joy_axis(player_index, field, 0)

    def _load_static_opponent(self, step_offset: int):
        if step_offset <= 0:
            raise ValueError(f"step_offset must be positive, got {step_offset}")

        weighted = []
        if CHECKPOINT_DIR.is_dir():
            for path in CHECKPOINT_DIR.glob("*.zip"):
                match = CHECKPOINT_STEPS_RE.search(path.name)
                if match is None:
                    print(f"[RoAEnv] skip opponent {path.name}: no step count in filename")
                    continue
                steps = int(match.group(1))
                weight = np.sqrt(steps / step_offset)
                if weight <= 0:
                    print(f"[RoAEnv] skip opponent {path.name}: non-positive weight")
                    continue
                weighted.append((path, weight))

        expected = tuple(self.observation_space.shape)
        while weighted:
            weights = np.array([w for _, w in weighted], dtype=np.float64)
            probs = weights / weights.sum()
            idx = int(self.np_random.choice(len(weighted), p=probs))
            path, _ = weighted.pop(idx)

            try:
                model = PPO.load(str(path))
            except Exception as e:
                print(f"[RoAEnv] skip opponent {path.name}: failed to load ({e})")
                continue
            loaded = tuple(model.observation_space.shape)
            if loaded != expected:
                print(
                    f"[RoAEnv] skip opponent {path.name}: "
                    f"obs {loaded} != env {expected}"
                )
                continue
            print(f"[RoAEnv] loaded static opponent from {path}")
            return model

        print("[RoAEnv] no compatible opponent checkpoint found; opponent will idle")
        return None

    def _state_to_obs(self, state: dict, player_index: int) -> np.ndarray:
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
            float(player_index),
            float(stage),
            float(clock),
            float(teams_enabled),
        ])

        return np.array(values, dtype=np.float32)
