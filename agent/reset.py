from bridge import Bridge
import time
import random
import numpy as np
from pathlib import Path
import re
from enum import Enum
from stable_baselines3 import PPO

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
        weights = [1/2, 1/2, 0, 0]
        return int(np.random.choice(4, p=weights))

    # pick a random number of opponents from [1, 3]
    def random_num_opponents(self) -> int:
        weights = [1, 0, 0] # for now, only one opponent
        return int(np.random.choice(3, p=weights) + 1)
    
    # pick a random list of active indexes, based on number of players
    def random_active_indexes(self, agent_index: int = 0, num_opponents: int = 1) -> list[bool]:
        active_indexes = [False] * 3
        for i in range(num_opponents):
            active_indexes[i] = True
        np.random.shuffle(active_indexes)
        active_indexes.insert(agent_index, True)
        return active_indexes

    # pick a random list of character choices [2, 3], based on number of players
    # for unactive players, the choice is 0
    def random_character_choices(self, active_indexes: list[bool] = [True, True, True, True]) -> list[int]:
        choices = [0] * 4
        for i in range(4):
            if active_indexes[i] == True:
                choices[i] = random.randint(2, 3)
        return choices

    # create a list of static opponents at their player indices
    def random_static_opponents(self, agent_index: int = 0, active_indexes: list[bool] = [True, True, False, False]) -> list: 
        opponents = [None] * 4
        for i in range(len(active_indexes)):
            if(i == agent_index):
                opponents[i] = None
            elif(active_indexes[i]):
                opponents[i] = self._load_static_opponent()
            else:
                opponents[i] = None
        return opponents

    # pick a random number of stocks for each player [2, 99]
    def random_player_stocks(self) -> list[int]:
        return [random.randint(2, 99) for _ in range(4)]

    # takes the list of character choices 0 = off, 1 = random, 2 = zetterburn, etc.
    # starts the match with the given character choices
    def restart_match(self, self_index: int = 0, character_choices: list = [0, 0, 0, 0], player_stocks: list[int] = [3, 3, 3, 3]) -> bool:
        
        # make a list of player slots that should be open
        open_indexes = [False] * 4
        open_indexes[self_index] = True
        for i in range(len(character_choices)):
            if character_choices[i] != 0:
                open_indexes[i] = True

        game_state = GameState.UNACTIONABLE
        while True:
            game_state = self._detect_game_state()

            match game_state:
                case GameState.POST_MATCH:
                    self._press_all(open_indexes, "a", 0.05, 0.0)

                case GameState.CHARACTER_SELECT:
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
                    time.sleep(1)

                case GameState.MAP_SELECT:
                    self._tap_direction("dleft", self_index, 0.1, 0.5)
                    self._press_all(open_indexes, "a", 0.05, 0.1)

                case GameState.UNACTIONABLE:
                    time.sleep(0.01)

                case GameState.MATCH:
                    # set player stocks
                    for i in range(4):
                        if player_stocks[i] > 0:
                            self.bridge.set_player_stocks(i, player_stocks[i])
                    print("[RoAEnv] match started")
                    # wait for countdown
                    deadline = time.time() + 10.0
                    while not self.bridge.get_can_make_inputs():
                        if time.time() >= deadline:
                            print("[RoAEnv] timed out waiting for match inputs")
                            break
                        if not self.bridge.get_game_is_running():
                            break
                        time.sleep(0.01)
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
        if self.bridge.get_is_post_match():
            return GameState.POST_MATCH
        if self.bridge.get_game_is_running():
            return GameState.MATCH
        elif (state == 987 or state == 989) and not self.bridge.get_is_map_selection():
            return GameState.CHARACTER_SELECT
        elif (state == 987 or state == 989) and self.bridge.get_is_map_selection():
            return GameState.MAP_SELECT
        else:
            return GameState.UNACTIONABLE

    def _load_static_opponent(self):
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
            choice = int(np.random.choice(len(weighted), p=probs))
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
            return model

        # default to idle opponent
        print("[RoAEnv] opponent will idle")
        return None

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
        agent_index = reset_manager.random_agent_index()
        active_indexes = reset_manager.random_active_indexes(agent_index)
        character_choices = reset_manager.random_character_choices(active_indexes)
        if i % 100 == 0:
            print(f"Character choices: {active_indexes} -> {character_choices}")
        for choice in character_choices:
            if choice == 2:
                hist_character_choices[0] += 1
            elif choice == 3:
                hist_character_choices[1] += 1
    print(f"num zetterburns: {hist_character_choices[0]}")
    print(f"num orcane: {hist_character_choices[1]}")

    # random player stocks
    print("\nRandom player stocks:")
    avg_stocks = 0
    for i in range(1000):
        stocks = reset_manager.random_player_stocks()
        avg_stocks += sum(stocks)
        if i % 100 == 0:
            print(f"Player stocks: {stocks}")
    print(f"Average player stocks: {avg_stocks / 1000 / 4}")

    # restart match
    print("\nRestart match:")
    agent_index = reset_manager.random_agent_index()
    n_opponents = reset_manager.random_num_opponents()
    active_indexes = reset_manager.random_active_indexes(agent_index, n_opponents)
    choices = reset_manager.random_character_choices(active_indexes)
    stocks = reset_manager.random_player_stocks()
    print(f"agent: {agent_index}, opps: {n_opponents} -> {active_indexes} -> {choices} & {stocks}")
    if reset_manager.restart_match(agent_index, choices, stocks):
        print("Match restarted successfully")
    else:
        print("Failed to restart match")

    time.sleep(15)

    # quit match
    print("\nQuit match:")
    if reset_manager.quit_match(agent_index):
        print("Match quit successfully")
    else:
        print("Failed to quit match")