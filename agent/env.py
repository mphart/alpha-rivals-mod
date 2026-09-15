"""
Gymnasium environment for Rivals of Aether, driven through the injected
DLL bridge (agent_bridge.Bridge).

Self play
"""

import time

import numpy as np
import gymnasium as gym
from gymnasium import spaces
from stable_baselines3 import PPO

from bridge import Bridge
from reward import RewardManager
from reset import ResetManager

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
        step_duration: float = 1.0 / FPS,  # how long an action is held, seconds (game is 60 fps, so 1/30 is 30 fps)
        max_episode_steps: int = 100 * 60 * FPS,   # 100 minutes of game time at FPS steps/sec
    ):
        super().__init__()

        self.bridge = Bridge()
        self.reward_manager = RewardManager()
        self.reset_manager = ResetManager(self.bridge)

        self.step_duration = step_duration
        self.max_episode_steps = max_episode_steps

        self.num_opponents = 0
        self.opponents = []
        self.agent_index = 0
        self.prev_state = None
        self.steps_this_episode = 0
        self.prev_buttons_held = {
            0: {f: False for f in BUTTON_FIELDS},
            1: {f: False for f in BUTTON_FIELDS},
        }

        # ---- Action space ----
        # Continuous: one entry per button, [-1, 1], 1 is down, -1 is up
        # Continuous: one entry per stick axis, [-1, 1]
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

        print(f"[RoAEnv] connected to {self.bridge.pipe_name}", flush=True)

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        self._release_all_joysticks()

        self.agent_index = self.reset_manager.random_agent_index()
        self.num_opponents = self.reset_manager.random_num_opponents()
        self.opponents = self.reset_manager.random_static_opponents(self.agent_index, self.num_opponents)

        self.reset_manager.reset()

        # get the initial state
        state = self.bridge.get_state()
        obs = self._state_to_obs(state, self.agent_index)
        self.prev_state = state
        self.steps_this_episode = 0

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

    def _release_all_joysticks(self):
        for player_index in (0, 1):
            prev = self._prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                prev[field] = False
            self.bridge.release_joy(player_index)

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
