from macro import MatchMacro
from bridge import Bridge
import time

class ResetManager():
    def __init__(self, bridge: Bridge):
        self.bridge = bridge
        self.match_macro = MatchMacro(self.bridge)

    def reset(self):
        # wait a few seconds to get to the post-match screen
        time.sleep(8.5)

        success = self.match_macro.restart_match(0, 1)

        return success