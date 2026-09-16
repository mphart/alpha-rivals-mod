"""
Script for playing a match against a model.
"""

from bridge import Bridge
from stable_baselines3 import PPO

AGENT_INDEX = 1
AGENT_MODEL = './legacy/selfplay_zetter_1v1_1811301_steps.zip'

def main(agent_index: int, agent_model: str, agent_character: int = 2):
    bridge = Bridge()

    model = PPO.load(agent_model)

    while True:
        # detect state
        state = ''

        match state:
            case 'character_select':
                # choose character
            case 'map_select':
                # do nothing
            case 'match':
                # act
            case 'post_match':
                # press 'a'
            case _:
                print(f"Unknown state: {state}")




if __name__ == "__main__":
    main()