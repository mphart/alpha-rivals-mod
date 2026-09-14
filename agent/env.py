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


def _normalize(value: float, lo: float, hi: float) -> float:
    return 2.0 * (value - lo) / (hi - lo) - 1.0


class RoAEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(
        self,
        training_steps: int = 10_000,
        self_player_index: int = 0,
        opponent_player_index: int = 1,
        step_duration: float = 1.0 / FPS,  # how long an action is held, seconds (game is 60 fps, so 1/30 is 30 fps)
        max_episode_steps: int = 100 * 60 * FPS,   # 100 minutes of game time at FPS steps/sec
        step_offset: int = 50_000,  # checkpoint save interval; used to weight opponent sampling
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
                np.full(len(BUTTON_FIELDS), -1.0, dtype=np.float32),
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
        self.num_game_values = 4

        self.values_per_player = 25
        self.num_player_slots = 4

        self.num_projectile_slots = 8
        self.values_per_projectile = 6

        self.num_ground_fire_slots = 6
        self.values_per_ground_fire = 3

        self.num_bubble_slots = 60
        self.values_per_bubble = 5

        self.num_puddle_slots = 60
        self.values_per_puddle = 3

        obs_dim = (
            self.num_player_slots * self.values_per_player
            + self.num_projectile_slots * self.values_per_projectile
            + self.num_ground_fire_slots * self.values_per_ground_fire
            + self.num_bubble_slots * self.values_per_bubble
            + self.num_puddle_slots * self.values_per_puddle
            + self.num_game_values
        )
        self.observation_space = spaces.Box(
            low=-1e6, high=1e6, shape=(obs_dim,), dtype=np.float32
        )

        self._steps_this_episode = 0
        self._prev_buttons_held = {
            0: {f: False for f in BUTTON_FIELDS},
            1: {f: False for f in BUTTON_FIELDS},
        }

        print(f"[RoAEnv] connected to {self.bridge.pipe_name}", flush=True)
        # Opponent is sampled on reset so we do not load a checkpoint twice at startup.
        self.opponent_model = None

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        # Randomly pick which slot the learner controls this episode.
        # Use the env RNG so seeding actually affects the coin flip.
        self.self_player_index = int(self.np_random.integers(0, 2))
        self.opponent_player_index = 1 - self.self_player_index

        self._release_all_joysticks()
        self.reset_manager.reset()

        # get the initial state
        state = self.bridge.get_state()
        obs = self._state_to_obs(state, self.self_player_index)
        self.prev_state = state
        self._steps_this_episode = 0

        # try to load a new opponent model
        self.opponent_model = self._load_static_opponent(self.step_offset)
        self._sync_joy_overrides()

        info = {}
        return obs, info

    def step(self, action):

        # get opponent action
        action_opponent = None
        if self.opponent_model is not None:
            obs_opponent = self._state_to_obs(self.prev_state, self.opponent_player_index)
            action_opponent, _ = self.opponent_model.predict(
                obs_opponent, deterministic=True
            )

        # apply actions
        self._apply_action(action, self.self_player_index)
        if action_opponent is not None:
            self._apply_action(action_opponent, self.opponent_player_index)

        # Hold the action for a fixed slice of real time
        time.sleep(self.step_duration)

        # get the new state
        curr_state = self.bridge.get_state()
        obs = self._state_to_obs(curr_state, self.self_player_index)

        # compute the reward
        reward, terminated = self.reward_manager.compute_reward(self.prev_state, curr_state, self.self_player_index, self.opponent_player_index)
        self.prev_state = curr_state

        self._steps_this_episode += 1
        truncated = self._steps_this_episode >= self.max_episode_steps
        info = {}
        return obs, reward, terminated, truncated, info

    def close(self):
        try:
            self._release_all_joysticks()
        except Exception:
            pass
        self.bridge.close()

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _apply_action(self, action, player_index):
        action = np.asarray(action, dtype=np.float32).reshape(-1)
        num_buttons = len(BUTTON_FIELDS)
        prev = self._prev_buttons_held[player_index]
        self.bridge.set_joy_override(player_index, True)

        for i, field in enumerate(BUTTON_FIELDS):
            down = bool(action[i] > 0)
            if down != prev[field]:
                self.bridge.set_joy_button(player_index, field, down)
                prev[field] = down

        for j, field in enumerate(STICK_FIELDS):
            raw = float(np.clip(action[num_buttons + j], -1.0, 1.0))
            value = int(round(raw * STICK_MAX))
            self.bridge.set_joy_axis(player_index, field, value)

    def _controlled_joy_indexes(self):
        indexes = {self.self_player_index}
        if self.opponent_model is not None:
            indexes.add(self.opponent_player_index)
        return indexes

    def _sync_joy_overrides(self):
        claimed = self._controlled_joy_indexes()
        for player_index in (0, 1):
            if player_index in claimed:
                self.bridge.set_joy_override(player_index, True)
            else:
                self.bridge.release_joy(player_index)

    def _release_all_joysticks(self):
        for player_index in (0, 1):
            prev = self._prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                prev[field] = False
            self.bridge.release_joy(player_index)

    def _load_static_opponent(self, step_offset: int):
        if step_offset <= 0:
            raise ValueError(f"step_offset must be positive, got {step_offset}")

        weighted = [(None, 2.0)]  # idle opponent
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

            if path is None:
                print("[RoAEnv] opponent will idle")
                return None

            try:
                model = PPO.load(str(path), device="cpu")
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
                    _normalize(float(p.get("percent", 0.0)), 0.0, 999.0),
                    _normalize(float(p.get("stock", 0.0)), 0.0, 99.0),
                    _normalize(float(p.get("x", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("y", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("url", 0.0)), 0.0, 19.0),
                    _normalize(float(p.get("state", 0.0)), 0.0, 350.0),
                    _normalize(float(p.get("state_timer", 0.0)), 0.0, 350.0),
                    _normalize(float(p.get("prev_state", 0.0)), 0.0, 350.0),
                    _normalize(float(p.get("prev_prev_state", 0.0)), 0.0, 350.0),
                    _normalize(float(p.get("attack", 0.0)), 0.0, 250.0),
                    _normalize(float(p.get("spr_dir", 0.0)), -1.0, 1.0),
                    _normalize(float(p.get("hsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("vsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("has_walljump", 0.0)), 0.0, 1.0),
                    _normalize(float(p.get("has_airdodge", 0.0)), 0.0, 1.0),
                    _normalize(float(p.get("djumps", 0.0)), 0.0, 3.0),
                    _normalize(float(p.get("attack_invince", 0.0)), 0.0, 500.0),
                    _normalize(float(p.get("respawn_invince_time", 0.0)), 0.0, 500.0),
                    _normalize(float(p.get("hitstop", 0.0)), 0.0, 100.0),
                    _normalize(float(p.get("hitstop_full", 0.0)), 0.0, 100.0),
                    _normalize(float(p.get("strong_charge", 0.0)), 0, 60),
                    _normalize(float(p.get("window", 0.0)), 0, 50.0),
                    _normalize(float(p.get("window_timer", 0.0)), 0, 100.0),
                    _normalize(float(p.get("burn_timer", 0.0)), 0, 150.0),
                ])
            else:
                values.extend([0.0] * self.values_per_player)

        projectiles = state.get("projectiles", [])
        for i in range(self.num_projectile_slots):
            if i < len(projectiles):
                p = projectiles[i]
                values.extend([
                    _normalize(float(p.get("x", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("y", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("hsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("vsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("spr_dir", 0.0)), -1.0, 1.0),
                    _normalize(float(p.get("player", 0.0)), 0.0, 4.0),
                ])
            else:
                values.extend([0.0] * self.values_per_projectile)

        ground_fires = state.get("ground_fires") or state.get("ground") or []
        for i in range(self.num_ground_fire_slots):
            if i < len(ground_fires):
                p = ground_fires[i]
                values.extend([
                    _normalize(float(p.get("x", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("y", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("player", 0.0)), 0.0, 4.0),
                ])
            else:
                values.extend([0.0] * self.values_per_ground_fire)

        bubbles = state.get("bubbles", [])
        for i in range(self.num_bubble_slots):
            if i < len(bubbles):
                p = bubbles[i]
                values.extend([
                    _normalize(float(p.get("x", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("y", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("hsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("vsp", 0.0)), -250.0, 250.0),
                    _normalize(float(p.get("player", 0.0)), 0.0, 4.0),
                ])
            else:
                values.extend([0.0] * self.values_per_bubble)

        puddles = state.get("puddles", [])
        for i in range(self.num_puddle_slots):
            if i < len(puddles):
                p = puddles[i]
                values.extend([
                    _normalize(float(p.get("x", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("y", 0.0)), -1500.0, 1500.0),
                    _normalize(float(p.get("player", 0.0)), 0.0, 4.0),
                ])
            else:
                values.extend([0.0] * self.values_per_puddle)

        game = state.get("game", {})
        stage = game.get("stage", 939)
        clock = game.get("clock", 0.0)
        teams_enabled = game.get("teams_enabled", 0.0)
        values.extend([
            _normalize(float(player_index), 0.0, 4.0),
            _normalize(float(stage), 939.0, 1200.0),
            _normalize(float(clock), 0.0, 360_000.0),
            _normalize(float(teams_enabled), 0.0, 1.0), 
        ])

        return np.array(values, dtype=np.float32)
