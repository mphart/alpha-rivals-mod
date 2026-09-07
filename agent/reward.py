"""
Reward manager for the RoA environment.
This class is used to compute the reward for a given agent index.
It is used to compute the reward for the agent based on the previous and current state of the game.
"""

class RewardManager():
    def __init__(self):
        self.existence_reward = 0.001
        self.stock_loss_reward = 1.0
        self.percent_loss_reward = 0.01

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
        # if (curr_opponent_pct > prev_opponent_pct):
        #     pct_diff = curr_opponent_pct - prev_opponent_pct
        #     reward += pct_diff * self.percent_loss_reward
        # if(curr_self_pct > prev_self_pct):
        #     pct_diff = curr_self_pct - prev_self_pct
        #     reward -= pct_diff * self.percent_loss_reward / 2
        
        # stock related rewards - just don't die
        if(curr_self_stock < prev_self_stock):
            # print("Self stock decreased, setting reward to: ", -1 * self.stock_loss_reward)
            reward = -1 * self.stock_loss_reward
        # if(curr_opponent_stock < prev_opponent_stock):
        #     print("Opponent stock decreased, setting reward to: ", self.stock_loss_reward / 2)
        #     reward += self.stock_loss_reward / 2

        # check if the match has ended
        if curr_opponent_stock == 0 or curr_self_stock == 0:
            terminated = True

        # clamp the reward to be between -1 and 1
        reward = max(-1, min(1, reward))

        print("Reward: ", reward)

        return reward, terminated
