"""
Reward manager for the RoA environment.
This class is used to compute the reward for a given agent index.
It is used to compute the reward for the agent based on the previous and current state of the game.
"""

class RewardManager():
    def __init__(self):
        self.self_existence_reward = 0.001
        self.self_stock_loss_reward = -5
        self.self_percent_gain_reward = -0.003
        self.self_action_reward = -0.0005
        self.opponent_existence_reward = 0
        self.opponent_stock_loss_reward = 3
        self.opponent_percent_gain_reward = 0.01

    # helper to get the player state from the state dict
    def _player(self, state: dict | None, index: int) -> dict:
        if not state:
            return {}
        players = state.get("players") or []
        if index < 0 or index >= len(players):
            return {}
        player = players[index]
        return player if isinstance(player, dict) else {}

    def compute_reward(self, prev_state: dict, curr_state: dict, agent_index: int, agent_action: list[int] = None) -> float:
        # get the past and present player states
        curr_players = [
            self._player(curr_state, i) for i in range(4)
        ]
        prev_players = [
            self._player(prev_state, i) for i in range(4)
        ]

        # initialize return value
        reward = 0

        for i in range(4):
            if curr_players[i] == {} or curr_players[i].get("on") == False:
                continue

            elif i == agent_index:
                # stock + existence
                curr_stock = curr_players[i].get("stock")
                prev_stock = prev_players[i].get("stock")
                if curr_stock is not None and prev_stock is not None:
                    if curr_stock < prev_stock:
                        reward += self.self_stock_loss_reward
                    else:
                        reward += self.self_existence_reward
                # percent
                curr_percent = curr_players[i].get("percent")
                prev_percent = prev_players[i].get("percent")
                if curr_percent is not None and prev_percent is not None:
                    if curr_percent > prev_percent:
                        diff = curr_percent - prev_percent
                        reward += self.self_percent_gain_reward * diff
                # action
                if agent_action is not None:
                    for j, action in enumerate(agent_action):
                        if action != 0:
                            reward += self.self_action_reward

            else:
                # stock + existence
                curr_stock = curr_players[i].get("stock")
                prev_stock = prev_players[i].get("stock")
                if curr_stock is not None and prev_stock is not None:
                    if curr_stock < prev_stock:
                        reward += self.opponent_stock_loss_reward
                    else:
                        reward += self.opponent_existence_reward
                # percent
                curr_percent = curr_players[i].get("percent")
                prev_percent = prev_players[i].get("percent")
                if curr_percent is not None and prev_percent is not None:
                    if curr_percent > prev_percent:
                        diff = curr_percent - prev_percent
                        reward += self.opponent_percent_gain_reward * diff

        return reward
