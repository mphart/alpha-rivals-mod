"""
Gymnasium environment for Rivals of Aether, driven through the injected
DLL bridge.

Self play implementation
"""

import time
import gymnasium as gym

from bridge import Bridge
from reward import RewardManager
from reset import ResetManager
from obs import ObservationManager
from input import InputManager

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
        self.input_manager = InputManager(self.bridge)
        self.reset_manager = ResetManager(self.bridge, self.input_manager)
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

        self.observation_space = self.obs_manager.get_obs_space()
        self.action_space = self.input_manager.get_action_space()

        print(f"[RoAEnv] connected to {self.bridge.pipe_name}", flush=True)

    # ------------------------------------------------------------------
    # Gym API
    # ------------------------------------------------------------------

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)

        self.input_manager.reset_all_inputs()

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
        self.input_manager.apply_action(action, self.agent_index)
        for opp_action, player_index in opponent_actions:
            self.input_manager.apply_action(opp_action, player_index)

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
            self.input_manager.release_menu_pads()
        except Exception:
            pass
        self.bridge.close()

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _is_terminal(self, state: dict) -> bool:
        game_running = self.bridge.get_game_is_running()
        clock = state.get("game", {}).get("clock")
        clock_expired = clock is not None and float(clock) <= 0.0
        players = state.get("players", [])
        num_alive_players = 0
        for i in range(4):
            if i < len(players) and players[i].get("on", False) and players[i].get("stock", 0) > 0:
                num_alive_players += 1
        return not game_running or num_alive_players <= 1 or clock_expired
