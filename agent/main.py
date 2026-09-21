"""
Script for playing a match against a model.
"""

from bridge import Bridge
from state import GameState, StateManager
from input import InputManager
from obs import ObservationManager
from stable_baselines3 import PPO
import time

FPS = 30

AGENT_INDEXES = [False, True, False, False]
AGENT_CHARACTER_CHOICES = [0, 2, 0, 0]
AGENT_MODEL = './checkpoints/zetterburn_1v1_5604810_steps.zip'

def main():
    bridge = Bridge()
    state_manager = StateManager(bridge)
    input_manager = InputManager(bridge)
    obs_manager = ObservationManager()

    model = PPO.load(AGENT_MODEL)

    input_manager.claim_menu_pads(AGENT_INDEXES)

    while True:
        state = state_manager.detect_game_state()

        if state == GameState.CHARACTER_SELECT:
            for i in range(len(AGENT_INDEXES)):
                if AGENT_INDEXES[i]:
                    bridge.set_player_on(i, True)
                    bridge.set_player_choice(i, AGENT_CHARACTER_CHOICES[i])
            time.sleep(2)

        elif state == GameState.MAP_SELECT:
            # do nothing
            pass

        elif state == GameState.MATCH:
            curr_state = bridge.get_state()
            obs = obs_manager.get_obs(curr_state, 1) # TODO loop
            (action, _) = model.predict(obs)
            input_manager.apply_action(action, 1)
            time.sleep(1 / FPS)

        elif state == GameState.POST_MATCH:
            for i in range(len(AGENT_INDEXES)):
                if AGENT_INDEXES[i]:
                    input_manager.reset_inputs(i)
                    input_manager.press("a", i, 0.05, 1)
            time.sleep(2)

        elif state == GameState.UNACTIONABLE:
            # do nothing
            pass



if __name__ == "__main__":
    main()