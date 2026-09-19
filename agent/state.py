from enum import Enum
from bridge import Bridge

class GameState(Enum):
    POST_MATCH = "post_match"
    CHARACTER_SELECT = "character_select"
    MAP_SELECT = "map_select"
    MATCH = "match"
    UNACTIONABLE = "unactionable"

class StateManager():
    def __init__(self, bridge: Bridge):
        self.bridge = bridge
    
    def detect_game_state(self) -> GameState:
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
