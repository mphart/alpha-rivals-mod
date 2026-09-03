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


# XInput button fields your dllmain.cpp already understands
BUTTON_FIELDS = ["a", "b", "x", "y", "lb"]  # trim/extend to match RoA's actual scheme
STICK_FIELDS = ["lx", "ly"]

# How many discrete stick positions to offer per axis (keeps the action
# space discrete and easy for PPO rather than needing a continuous head).
STICK_STEPS = [-32768, -4096, -2048, 0, 2048, 4096, 32767]  # left/neutral/right, or down/neutral/up


class RoAEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(
        self,
        controlled_joy: int = 0,
        opponent_player_index: int = 1,
        self_player_index: int = 0,
        step_duration: float = 1.0 / 30.0,  # how long an action is held, seconds (game is 60 fps, so 1/30 is 30 fps)
        max_episode_steps: int = 8 * 60 * 30,   # ~8 minutes of game time at 15 steps/sec
        existence_reward: float = 0.001,
        stock_loss_reward: float = 1.0,
        percent_loss_reward: float = 0.01,
    ):
        super().__init__()

        self.bridge = Bridge()
        self.controlled_joy = controlled_joy
        self.opponent_player_index = opponent_player_index
        self.self_player_index = self_player_index
        self.step_duration = step_duration
        self.max_episode_steps = max_episode_steps
        self.existence_reward = existence_reward
        self.stock_loss_reward = stock_loss_reward
        self.percent_loss_reward = percent_loss_reward

        # ---- Action space ----
        # MultiDiscrete: one entry per button (0=up,1=down), plus one entry
        # per stick axis (index into STICK_STEPS).
        self.action_space = spaces.MultiDiscrete(
            [2] * len(BUTTON_FIELDS) + [len(STICK_STEPS)] * len(STICK_FIELDS)
        )

        # ---- Observation space ----
        # Flattened: for each of up to 4 player slots -> [on, percent, stock,
        # x, y, anim, character, team] (8 values), plus game [speed, stage,
        # clock] (3 values). Adjust this if BuildGameStateJson's shape changes.
        self.num_player_slots = 4
        self.values_per_player = 8
        obs_dim = self.num_player_slots * self.values_per_player + 3
        self.observation_space = spaces.Box(
            low=-1e6, high=1e6, shape=(obs_dim,), dtype=np.float32
        )

        self._last_opponent_stock = None
        self._last_self_stock = None
        self._last_opponent_percent = None
        self._last_self_percent = None
        self._steps_this_episode = 0
        self._prev_buttons_held = {f: False for f in BUTTON_FIELDS}

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        self._release_all_buttons()
        self.match_reset() 

        state = self.bridge.get_state()
        obs = self._state_to_obs(state)

        self._last_opponent_stock = self._get_stock(state, self.opponent_player_index)
        self._last_self_stock = self._get_stock(state, self.self_player_index)
        self._last_opponent_percent = state.get("players", [self.opponent_player_index] if False else [])[self.opponent_player_index].get("percent", 0.0)
        self._steps_this_episode = 0

        info = {}
        return obs, info

    def step(self, action):
        self._apply_action(action)

        # Hold the action for a fixed slice of real time. This is a real
        # game running in real time, not a turn-based simulator, so we
        # can't "step" it directly -- we just wait.
        time.sleep(self.step_duration)

        state = self.bridge.get_state()
        obs = self._state_to_obs(state)

        reward, terminated = self._compute_reward(state)

        self._steps_this_episode += 1
        truncated = self._steps_this_episode >= self.max_episode_steps

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
            down = bool(action[i])
            if down != self._prev_buttons_held[field]:
                self.bridge.set_joy_button(self.controlled_joy, field, down)
                self._prev_buttons_held[field] = down

        for j, field in enumerate(STICK_FIELDS):
            step_index = int(action[num_buttons + j])
            value = STICK_STEPS[step_index]
            self.bridge.set_joy_axis(self.controlled_joy, field, value)

    def _release_all_buttons(self):
        for field in BUTTON_FIELDS:
            self.bridge.set_joy_button(self.controlled_joy, field, False)
            self._prev_buttons_held[field] = False
        for field in STICK_FIELDS:
            self.bridge.set_joy_axis(self.controlled_joy, field, 0)

    def _get_stock(self, state: dict, player_index: int):
        players = state.get("players", [])
        if player_index >= len(players):
            return None
        player = players[player_index]
        if not player.get("on", False):
            return None
        return player.get("stock")

    def _compute_reward(self, state: dict):
        """
        Minimal reward: small constant reward for existing, plus a reward
        whenever the opponent's stock count decreases. Episode ends when
        either player is no longer "on" (match likely over) or the
        opponent has run out of stocks.
        """
        reward = self.existence_reward
        terminated = False

        self_player = state.get("players", [self.self_player_index] if False else [])
        players = state.get("players", [])

        self_on = players[self.self_player_index].get("on", False) if len(players) > self.self_player_index else False
        opp_on = players[self.opponent_player_index].get("on", False) if len(players) > self.opponent_player_index else False

        if not self_on or not opp_on:
            # Match likely ended or a player slot dropped out -- end episode,
            # no extra reward/penalty since we're deliberately keeping this
            # reward signal minimal per the experiment design.
            terminated = True
            return reward, terminated

        # stock related rewards
        current_opponent_stock = self._get_stock(state, self.opponent_player_index)
        current_self_stock = self._get_stock(state, self.self_player_index)
        if (
            self._last_opponent_stock is not None
            and current_opponent_stock is not None
            and current_opponent_stock < self._last_opponent_stock
        ):
            print("Opponent stock decreased, adding reward")
            reward += self.stock_loss_reward
        if(
            self._last_self_stock is not None
            and current_self_stock is not None
            and current_self_stock < self._last_self_stock
        ):
            print("Self stock decreased, decreasing reward")
            reward -= self.stock_loss_reward
        
        # percent related rewards
        current_opponent_percent = players[self.opponent_player_index].get("percent", 0.0)
        current_self_percent = players[self.self_player_index].get("percent", 0.0)
        if (
            self._last_opponent_percent is not None
            and current_opponent_percent is not None
            and current_opponent_percent > self._last_opponent_percent
        ):
            pct_diff = current_opponent_percent - self._last_opponent_percent
            reward += pct_diff * self.percent_loss_reward
        if (
            self._last_self_percent is not None
            and current_self_percent is not None
            and current_self_percent > self._last_self_percent
        ):
            pct_diff = current_self_percent - self._last_self_percent
            reward -= pct_diff * self.percent_loss_reward

        # update last values
        if current_opponent_stock is not None:
            self._last_opponent_stock = current_opponent_stock
        if current_opponent_percent is not None:
            self._last_opponent_percent = current_opponent_percent
        if current_self_stock is not None:
            self._last_self_stock = current_self_stock
        if current_self_percent is not None:
            self._last_self_percent = current_self_percent

        if current_opponent_stock == 0:
            terminated = True
        if current_self_stock == 0:
            terminated = True

        return reward, terminated

    def _state_to_obs(self, state: dict) -> np.ndarray:
        values = []
        players = state.get("players", [])

        for i in range(self.num_player_slots):
            if i < len(players) and players[i].get("on", False):
                p = players[i]
                values.extend([
                    1.0,
                    float(p.get("percent", 0.0)),
                    float(p.get("stock", 0.0)),
                    float(p.get("x", 0.0)),
                    float(p.get("y", 0.0)),
                    float(p.get("anim", 0.0)),
                    float(p.get("character", 0.0)),
                    float(p.get("team", 0.0)),
                ])
            else:
                values.extend([0.0] * self.values_per_player)

        game = state.get("game", {})
        values.extend([
            float(game.get("speed", 1.0)),
            float(game.get("stage", 0.0)),
            float(game.get("clock", 0.0)),
        ])

        return np.array(values, dtype=np.float32)

    def match_reset(self):
        # wait a few seconds to get to the post-match screen
        time.sleep(8.5)

        macro = MatchMacro(self.bridge)

        print("Attempting to restart match...")
        success = macro.restart_match(self_index=0, opponent_index=1)
        return success