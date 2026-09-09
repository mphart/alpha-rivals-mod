"""
Reward manager for the RoA environment.
This class is used to compute the reward for a given agent index.
It is used to compute the reward for the agent based on the previous and current state of the game.
"""

class RewardManager():
    def __init__(self):
        self.existence_reward = 0.001
        self.self_stock_loss_reward = -5.0
        self.opponent_stock_loss_reward = 3.0
        self.self_percent_loss_reward = -0.005
        self.opponent_percent_loss_reward = 0.01
        self.self_win_reward = 2.0
        self.opponent_win_reward = -2.0

    def _player(self, state: dict | None, index: int) -> dict:
        if not state:
            return {}
        players = state.get("players") or []
        if index < 0 or index >= len(players):
            return {}
        player = players[index]
        return player if isinstance(player, dict) else {}

    def compute_reward(self, prev_state: dict, curr_state: dict, self_player_index: int, opponent_player_index: int):
        # initialize return values
        reward = self.existence_reward
        terminated = False

        curr_self = self._player(curr_state, self_player_index)
        curr_opp = self._player(curr_state, opponent_player_index)
        prev_self = self._player(prev_state, self_player_index)
        prev_opp = self._player(prev_state, opponent_player_index)

        curr_opponent_pct = curr_opp.get("percent", 0.0)
        curr_self_pct = curr_self.get("percent", 0.0)
        prev_opponent_pct = prev_opp.get("percent", 0.0)
        prev_self_pct = prev_self.get("percent", 0.0)

        curr_opponent_stock = curr_opp.get("stock", 0)
        curr_self_stock = curr_self.get("stock", 0)
        prev_opponent_stock = prev_opp.get("stock", 0)
        prev_self_stock = prev_self.get("stock", 0)

        # percent related rewards
        if (curr_opponent_pct > prev_opponent_pct):
            pct_diff = curr_opponent_pct - prev_opponent_pct
            reward += pct_diff * self.opponent_percent_loss_reward
        if(curr_self_pct > prev_self_pct):
            pct_diff = curr_self_pct - prev_self_pct
            reward += pct_diff * self.self_percent_loss_reward
        
        # stock related rewards
        if(curr_self_stock < prev_self_stock):
            reward += self.self_stock_loss_reward
            print('Self stock decreased, reward: ', reward)
        if(curr_opponent_stock < prev_opponent_stock):
            reward += self.opponent_stock_loss_reward
            print('Opponent stock decreased, reward: ', reward)

        # check if the match has ended
        if curr_opponent_stock == 0 or curr_self_stock == 0:
            reward += self.self_win_reward if curr_self_stock > curr_opponent_stock else self.opponent_win_reward
            terminated = True

        return reward, terminated
