"""
Gymnasium environment for Rivals of Aether, driven through the injected
DLL bridge (agent_bridge.Bridge).

Self play
"""

import time

import numpy as np
import gymnasium as gym
from gymnasium import spaces

from bridge import Bridge
from reward import RewardManager
from reset import ResetManager
from obs import ObservationManager

BUTTON_FIELDS = ["a", "b", "x", "y", "lb", "dup"] 
STICK_FIELDS = ["lx", "ly"]
STICK_MAX = 32767

FPS = 30


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
        self.obs_manager = ObservationManager()

        self.step_duration = step_duration
        self.max_episode_steps = max_episode_steps

        # reset
        self.agent_index = 0
        self.num_opponents = 0
        self.opponents = []
        self.character_choices = []
        self.active_indexes = []
        self.player_stocks = []
        
        # info & tracking
        self.prev_state = None
        self.steps_this_episode = 0
        self.prev_buttons_held = {
            0: {f: False for f in BUTTON_FIELDS},
            1: {f: False for f in BUTTON_FIELDS},
            2: {f: False for f in BUTTON_FIELDS},
            3: {f: False for f in BUTTON_FIELDS},
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

        self.observation_space = self.obs_manager.get_obs_space()

        print(f"[RoAEnv] connected to {self.bridge.pipe_name}", flush=True)

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        self._reset_all_inputs()

        # run the reset
        self.agent_index = self.reset_manager.random_agent_index()
        print(f"[RoAEnv] agent index: {self.agent_index}")
        self.num_opponents = self.reset_manager.random_num_opponents()
        self.active_indexes = [True, True, False, False] #self.reset_manager.random_active_indexes(self.agent_index, self.num_opponents)
        self.character_choices = self.reset_manager.random_character_choices(self.active_indexes)
        self.opponents = self.reset_manager.random_static_opponents(self.agent_index, self.active_indexes)
        self.player_stocks = self.reset_manager.random_player_stocks()

        self.reset_manager.restart_match(self.agent_index, self.character_choices, self.player_stocks)

        # get the initial state
        state = self.bridge.get_state()
        obs = self.obs_manager.get_obs(state, self.agent_index)
        self.prev_state = state
        self.steps_this_episode = 0

        info = {}
        return obs, info

    def step(self, action):

        # get opponent observations
        opponent_observations = []
        for i in range(4):
            if self.active_indexes[i] == True and self.opponents[i] is not None:
                # (observation, player_index)
                observation = self.obs_manager.get_obs(self.prev_state, i)
                opponent_observations.append((observation, i))

        # get opponent action(s)
        opponent_actions = []
        for opp_obs, index in opponent_observations:
            if self.active_indexes[index] == True and self.opponents[index] is not None:
                opp_action, _ = self.opponents[index].predict(opp_obs)
                opponent_actions.append((opp_action, index))

        # apply actions
        self._apply_action(action, self.agent_index)
        for opp_action, player_index in opponent_actions:
            self._apply_action(opp_action, player_index)

        # hold the actions for a fixed slice of real time
        time.sleep(self.step_duration)

        # get the new state
        curr_state = self.bridge.get_state()
        obs = self.obs_manager.get_obs(curr_state, self.agent_index)

        # compute the reward
        reward = self.reward_manager.compute_reward(self.prev_state, curr_state, self.agent_index)
        self.prev_state = curr_state

        # check for truncation
        self.steps_this_episode += 1
        truncated = self.steps_this_episode >= self.max_episode_steps
        if truncated:
            self.reset_manager.quit_match(self.agent_index)

        # check for termination
        terminated = not truncated and self._is_terminal(curr_state)
        if terminated:
            print("[RoAEnv] match terminated")

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

    def _is_terminal(self, state: dict) -> bool:
        game_running = self.bridge.get_game_is_running()
        clock = state.get("game", {}).get("clock", 0.0)
        num_alive_players = 0
        for i in range(4):
            players = state.get("players", [])
            if i < len(players) and players[i].get("on", False) == True and players[i].get("stock", 0) > 0:
                num_alive_players += 1
        return not game_running or num_alive_players <= 1 or clock <= 0.0

    def _apply_action(self, action, player_index):
        action = np.asarray(action, dtype=np.float32).reshape(-1)
        num_buttons = len(BUTTON_FIELDS)
        prev = self.prev_buttons_held[player_index]

        for i, field in enumerate(BUTTON_FIELDS):
            down = bool(action[i] > 0)
            if down != prev[field]:
                self.bridge.set_joy_button(player_index, field, down)
                prev[field] = down

        for j, field in enumerate(STICK_FIELDS):
            raw = float(np.clip(action[num_buttons + j], -1.0, 1.0))
            value = int(round(raw * STICK_MAX))
            self.bridge.set_joy_axis(player_index, field, value)
    
    def _reset_all_inputs(self):
        for player_index in range(4):
            for field in BUTTON_FIELDS:
                self.bridge.set_joy_button(player_index, field, False)
            for field in STICK_FIELDS:
                self.bridge.set_joy_axis(player_index, field, 0)

    def _release_all_joysticks(self):
        for player_index in range(4):
            prev = self.prev_buttons_held[player_index]
            for field in BUTTON_FIELDS:
                prev[field] = False
            self.bridge.release_joy(player_index)
