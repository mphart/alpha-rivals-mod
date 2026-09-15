from bridge import Bridge
import time
import random
import numpy as np
from pathlib import Path
import re
import time
from enum import Enum

AGENT_DIR = Path(__file__).resolve().parent
CHECKPOINT_DIR = AGENT_DIR / "checkpoints"
CHECKPOINT_STEPS_RE = re.compile(r"_(\d+)_steps\.zip$", re.IGNORECASE)

IDLE_OPPONENT_WEIGHT = 400

class GameState(Enum):
    POST_MATCH = "post_match"
    CHARACTER_SELECT = "character_select"
    MAP_SELECT = "map_select"
    MATCH = "match"
    UNACTIONABLE = "unactionable"


class ResetManager():
    def __init__(self, bridge: Bridge):
        self.bridge = bridge

    # pick a random agent index from [0, 3]
    def random_agent_index(self) -> int:
        weights = [1/4, 1/4, 1/4, 1/4]
        return int(np.random.choice(4, p=weights))

    # pick a random number of opponents from [1, 3]
    def random_num_opponents(self) -> int:
        weights = [3/6, 2/6, 1/6]
        return int(np.random.choice(3, p=weights) + 1)
    
    # pick a random list of active indexes, based on number of players
    def random_active_indexes(self, agent_index: int = 0, num_opponents: int = 1) -> list[bool]:
        active_indexes = [False] * 3
        for i in range(num_opponents):
            active_indexes[i] = True
        np.random.shuffle(active_indexes)
        active_indexes.insert(agent_index, True)
        return active_indexes

    # pick a random list of character choices, based on number of players
    def random_character_choices(self, active_indexes: list[bool] = [True, True, True, True]) -> list[int]:
        choices = [0] * 4
        for i in range(4):
            choices[i] = random.randint(2, 3) # [2,3]
        return choices

    # create a list of static opponents at their player indices
    def random_static_opponents(self, agent_index: int = 0, active_indexes: list[bool] = [True, True, False, False]) -> list: 
        opponents = [None] * 4
        for i in range(active_indexes):
            if(i == agent_index):
                opponents[i] = None
            elif(active_indexes[i]):
                opponents[i] = self._load_static_opponent()
            else:
                opponents[i] = None
        return opponents

    # takes the list of character choices 0 = off, 1 = random, 2 = zetterburn, etc.
    # starts the match with the given character choices
    def restart_match(self, self_index: int = 0, character_choices: list = [0, 0, 0, 0]) -> bool:
        # make a list of player slots that should be open
        open_indexes = [False] * 4
        open_indexes[self_index] = True
        for i in range(len(character_choices)):
            if character_choices[i] != 0:
                open_indexes[i] = True

        game_state = GameState.UNACTIONABLE
        while game_state != GameState.MATCH:
            game_state = self._detect_game_state()

            match game_state:
                case GameState.POST_MATCH:
                    print("Post match game state")
                    self._press_all(open_indexes, "a", 0.05, 0.0)

                case GameState.CHARACTER_SELECT:
                    print("Character select game state")
                    # claim menu pads
                    self._claim_menu_pads(open_indexes)
                    # set players on/off
                    for i in range(len(open_indexes)):
                        if open_indexes[i] == True:
                            self.bridge.set_player_on(i, True)
                        else:
                            self.bridge.set_player_on(i, False)
                    # set character choices
                    for i in range(len(character_choices)):
                        if character_choices[i] != 0:
                            self.bridge.set_player_choice(i, character_choices[i])
                    # start match button
                    self._press("start", self_index, 0.05, 0.1)

                case GameState.MAP_SELECT:
                    print("Map select game state")
                    self._press_all(open_indexes, "a", 0.05, 0.1)

                case GameState.UNACTIONABLE:
                    print("Unactionable game state")
                    time.sleep(0.1)

                case GameState.MATCH:
                    print("Match game state")
                    return True
                case _:
                    raise ValueError(f"Invalid game state: {game_state}")

    def quit_match(self, self_index: int) -> bool:
        try:
            self._press("start", self_index, 0.05, 1)
            self._tap_direction("ddown", self_index, 0.05, 0.15)
            self._tap_direction("ddown", self_index, 0.05, 0.15)
            self._press("a", self_index, 0.05, 0.0)
            self._tap_direction("ddown", self_index, 0.05, 0.15)
            self._press("a", self_index, 0.05, 0.0)
            return True
        except:
            return False

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _detect_game_state(self) -> GameState:
        state = self.bridge.get_game_stage()
        match_running = self.bridge.get_game_is_running()
        print(f"Game state: {state}, Match running: {match_running}")
        if match_running:
            print("Match game state detected")
            return GameState.MATCH
        elif (state == 987 or state == 989) and not self.bridge.get_is_map_selection():
            print("Character select game state detected")
            return GameState.CHARACTER_SELECT
        elif (state == 987 or state == 989) and self.bridge.get_is_map_selection():
            print("Map select game state detected")
            return GameState.MAP_SELECT
        elif state == 1039:
            print("Post match game state detected")
            return GameState.POST_MATCH
        else:
            print("Unactionable game state detected")
            return GameState.UNACTIONABLE

    def _load_static_opponent(self) -> int:
        # (path, steps, weight)
        weighted = [(None, 0, IDLE_OPPONENT_WEIGHT)]  # idle opponent

        # check for candidates in the checkpoint directory
        if CHECKPOINT_DIR.is_dir():
            for path in CHECKPOINT_DIR.glob("*.zip"):
                match = CHECKPOINT_STEPS_RE.search(path.name)
                if match is None:
                    print(f"[RoAEnv] skip opponent {path.name}: no step count in filename")
                    continue
                steps = int(match.group(1))
                weight = np.sqrt(steps)
                weighted.append((path, steps, weight))

        # expected = tuple(self.observation_space.shape)
        while weighted:
            weights = np.array([w for _, _, w in weighted], dtype=np.float64)
            probs = weights / weights.sum()
            choice = int(self.np_random.choice(len(weighted), p=probs))
            path, steps, _ = weighted.pop(choice)

            if path is None:
                print("[RoAEnv] opponent will idle")
                return None

            try:
                model = PPO.load(str(path), device="cpu")
            except Exception as e:
                print(f"[RoAEnv] skip opponent {path.name}: failed to load ({e})")
                continue

            print(f"[RoAEnv] loaded static opponent with {steps} steps")
            return steps

        # default to idle opponent
        print("[RoAEnv] opponent will idle")
        return 0

    # claim menu pads for the given indexes, or all 4 of them by default
    def _claim_menu_pads(self, indexes: list[bool] = [True, True, True, True]):
        for i, index in enumerate(indexes):
            if index:
                self.bridge.set_joy_override(i, True)

    # release all menu pads
    def _release_menu_pads(self):
        for i in range(4):
            self.bridge.release_joy(i)

    # tap a direction on the menu cursor
    def _tap_direction(self, field: str, joy_index: int, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(joy_index, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(joy_index, field, False)
        time.sleep(wait_after)

    # press a button
    def _press(self, field: str, joy_index: int, hold_seconds: float, wait_after: float):
        self.bridge.set_joy_button(joy_index, field, True)
        time.sleep(hold_seconds)
        self.bridge.set_joy_button(joy_index, field, False)
        time.sleep(wait_after)

    # press a button for all indexes that are True
    def _press_all(self, indexes: list[bool], field: str, hold_seconds: float, wait_after: float):
        for i, index in enumerate(indexes):
            if index:
                self._press(field, i, hold_seconds, wait_after)

if __name__ == "__main__":
    bridge = Bridge()
    reset_manager = ResetManager(bridge)

    # random agent index
    print("Random agent index:")
    avg_index = 0
    for i in range(1000):
        index = reset_manager.random_agent_index()
        if i % 100 == 0:
            print(f"Agent index: {index}")
        avg_index += index
    print(f"Average agent index: {avg_index / 1000}")

    # random number of opponents
    print("\nRandom number of opponents:")
    avg_opponents = 0
    for i in range(1000):
        n_opponents = reset_manager.random_num_opponents()
        if i % 100 == 0:
            print(f"Number of opponents: {n_opponents}")
        avg_opponents += n_opponents
    print(f"Average number of opponents: {avg_opponents / 1000}")

    # random active indexes
    print("\nRandom active indexes:")
    for i in range(1000):
        agent_index = reset_manager.random_agent_index()
        n_opponents = reset_manager.random_num_opponents()
        active_indexes = reset_manager.random_active_indexes(agent_index, n_opponents)
        if i % 100 == 0:
            print(f"agent: {agent_index}, opps: {n_opponents} -> {active_indexes}")

    # random character choices
    print("\nRandom character choices:")
    hist_character_choices = [0, 0]
    for i in range(1000):
        character_choices = reset_manager.random_character_choices()
        if i % 100 == 0:
            print(f"Character choices: {character_choices}")
        for choice in character_choices:
            if choice == 2:
                hist_character_choices[0] += 1
            elif choice == 3:
                hist_character_choices[1] += 1
            else:
                raise ValueError(f"Invalid character choice: {choice}")
    print(f"num zetterburns: {hist_character_choices[0]}")
    print(f"num orcane: {hist_character_choices[1]}")

    # restart match
    print("\nRestart match:")
    agent_index = reset_manager.random_agent_index()
    n_opponents = reset_manager.random_num_opponents()
    active_indexes = reset_manager.random_active_indexes(agent_index, n_opponents)
    choices = reset_manager.random_character_choices(active_indexes)
    print(f"agent: {agent_index}, opps: {n_opponents} -> {active_indexes} -> {choices}")
    if reset_manager.restart_match(agent_index, choices):
        print("Match restarted successfully")
    else:
        print("Failed to restart match")