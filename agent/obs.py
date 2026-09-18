"""
Take in a state and player index and return the observation for that player.

Since this is a 1v1 only model at the moment, the other player must be on
and will have its values placed after the target player's values.

Here is the structure of an observation for player_index p:

game
--------------------------------
0: clock
1-19: game stage values (stage id in slot 1; rest reserved)
--------------------------------

player p
--------------------------------
25 values per player
3 projectiles x 5 values
3 ground fires x 2 values
1 puddle x 2 values
60 bubbles x 4 values
--------------------------------

other player
--------------------------------
25 values per player
3 projectiles x 5 values
3 ground fires x 2 values
1 puddle x 2 values
60 bubbles x 4 values
--------------------------------
Total: 596 values
"""

import numpy as np
from gymnasium import spaces

MAX_PERCENT = 999.0
MAX_STOCK = 99.0
MAX_X = 2500.0
MAX_Y = 2500.0
MAX_HSP = 250.0
MAX_VSP = 250.0
MAX_WINDOW = 100.0
MAX_WINDOW_TIMER = 600.0
MAX_BURN_TIMER = 151.0
MAX_DJUMPS = 3.0
MAX_ATTACK_INVINCE = 500.0
MAX_RESPAWN_INVINCE_TIME = 500.0
MAX_HITSTOP = 120.0
MAX_HITSTOP_FULL = 120.0
MAX_STRONG_CHARGE = 60.0
MAX_URL = 19.0
MAX_STAGE = 1200.0
MIN_STAGE = 939.0
MAX_CLOCK = 360_000.0
MAX_ATTACK = 250.0
MAX_STATE = 1024.0

LEFT = -1.0
RIGHT = 1.0

TRUE = 1.0
FALSE = 0.0

OBS_DIM = 596


class ObservationManager:
    def __init__(self):
        self.num_game_values = 20
        self.num_player_slots = 4
        self.num_active_player_slots = 2
        self.values_per_active_player = 25

        self.num_projectile_slots_per_player = 3
        self.values_per_projectile = 5

        self.num_ground_fire_slots_per_player = 3
        self.values_per_ground_fire = 2

        self.num_puddle_slots_per_player = 1
        self.values_per_puddle = 2

        self.num_bubble_slots_per_player = 60
        self.values_per_bubble = 4

        per_player = (
            self.values_per_active_player
            + self.num_projectile_slots_per_player * self.values_per_projectile
            + self.num_ground_fire_slots_per_player * self.values_per_ground_fire
            + self.num_puddle_slots_per_player * self.values_per_puddle
            + self.num_bubble_slots_per_player * self.values_per_bubble
        )
        self.obs_dim = self.num_game_values + self.num_active_player_slots * per_player
        if self.obs_dim != OBS_DIM:
            raise ValueError(f"obs_dim {self.obs_dim} != {OBS_DIM}")

        self.observation_space = spaces.Box(
            low=-1e6, high=1e6, shape=(self.obs_dim,), dtype=np.float32
        )

    def get_obs_dim(self) -> int:
        return self.obs_dim

    def get_obs_space(self) -> spaces.Box:
        return self.observation_space

    def get_obs(self, state: dict, target_player_index: int) -> np.ndarray:
        game = state.get("game", {})
        stage_slots = [0.0] * 19
        stage_slots[0] = self._normalize(
            float(game.get("stage", MIN_STAGE)), MIN_STAGE, MAX_STAGE
        )
        values = [
            self._normalize(float(game.get("clock", 0.0)), 0.0, MAX_CLOCK),
            *stage_slots,
        ]

        players = state.get("players", [])
        target_player = self._player_at(players, target_player_index)
        other_index = self._other_player_index(players, target_player_index)
        other_player = self._player_at(players, other_index) if other_index is not None else None

        projectiles = state.get("projectiles", [])
        ground_fires = state.get("ground_fires") or state.get("ground") or []
        puddles = state.get("puddles", [])
        bubbles = state.get("bubbles", [])

        values.extend(self._player_block(
            target_player_index, target_player, projectiles, ground_fires, puddles, bubbles
        ))
        values.extend(self._player_block(
            other_index, other_player, projectiles, ground_fires, puddles, bubbles
        ))

        obs = np.array(values, dtype=np.float32)
        expected = int(self.observation_space.shape[0])
        if obs.shape[0] != expected:
            raise ValueError(f"obs length {obs.shape[0]} != observation_space {expected}")
        return obs

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _player_block(
        self,
        slot,
        player: dict | None,
        projectiles: list,
        ground_fires: list,
        puddles: list,
        bubbles: list,
    ) -> list:
        values = (
            self._get_player_values(player)
            if player is not None
            else [0.0] * self.values_per_active_player
        )
        if slot is None:
            values.extend([0.0] * (
                self.num_projectile_slots_per_player * self.values_per_projectile
                + self.num_ground_fire_slots_per_player * self.values_per_ground_fire
                + self.num_puddle_slots_per_player * self.values_per_puddle
                + self.num_bubble_slots_per_player * self.values_per_bubble
            ))
            return values

        values.extend(self._owned_values(
            projectiles, slot, self.num_projectile_slots_per_player,
            self.values_per_projectile, self._get_projectile_values
        ))
        values.extend(self._owned_values(
            ground_fires, slot, self.num_ground_fire_slots_per_player,
            self.values_per_ground_fire, self._get_fire_values
        ))
        values.extend(self._owned_values(
            puddles, slot, self.num_puddle_slots_per_player,
            self.values_per_puddle, self._get_puddle_values
        ))
        values.extend(self._owned_values(
            bubbles, slot, self.num_bubble_slots_per_player,
            self.values_per_bubble, self._get_bubble_values
        ))
        return values

    def _player_at(self, players: list, index: int | None) -> dict | None:
        if index is None or index < 0 or index >= len(players):
            return None
        player = players[index]
        if not player.get("on", False):
            return None
        return player

    def _other_player_index(self, players: list, target_player_index: int) -> int | None:
        for i, player in enumerate(players):
            if i == target_player_index:
                continue
            if player.get("on", False):
                return i
        return None

    def _owner_slot(self, entity: dict) -> int | None:
        raw = entity.get("player")
        if raw is None:
            return None
        try:
            return int(raw) - 1
        except (TypeError, ValueError):
            return None

    def _owned_values(self, items: list, slot: int, limit: int, values_per: int, encode) -> list:
        values = []
        taken = 0
        for item in items:
            if self._owner_slot(item) != slot:
                continue
            values.extend(encode(item))
            taken += 1
            if taken >= limit:
                break
        needed = limit * values_per
        if len(values) < needed:
            values.extend([0.0] * (needed - len(values)))
        return values

    def _normalize(self, value: float, lo: float, hi: float) -> float:
        val = 2.0 * (value - lo) / (hi - lo) - 1.0
        if val < -1.0 or val > 1.0:
            val = float(np.clip(val, -1, 1))
        return val

    def _get_player_values(self, player: dict) -> list:
        return [
            self._normalize(float(player.get("on", 0.0)), FALSE, TRUE),
            self._normalize(float(player.get("percent", 0.0)), 0.0, MAX_PERCENT),
            self._normalize(float(player.get("stock", 0.0)), 0.0, MAX_STOCK),
            self._normalize(float(player.get("x", 0.0)), -MAX_X, MAX_X),
            self._normalize(float(player.get("y", 0.0)), -MAX_Y, MAX_Y),
            self._normalize(float(player.get("url", 0.0)), 0.0, MAX_URL),
            self._normalize(float(player.get("state", 0.0)), 0.0, MAX_STATE),
            self._normalize(float(player.get("state_timer", 0.0)), 0.0, MAX_STATE),
            self._normalize(float(player.get("prev_state", 0.0)), 0.0, MAX_STATE),
            self._normalize(float(player.get("prev_prev_state", 0.0)), 0.0, MAX_STATE),
            self._normalize(float(player.get("attack", 0.0)), 0.0, MAX_ATTACK),
            self._normalize(float(player.get("spr_dir", 0.0)), LEFT, RIGHT),
            self._normalize(float(player.get("hsp", 0.0)), -MAX_HSP, MAX_HSP),
            self._normalize(float(player.get("vsp", 0.0)), -MAX_VSP, MAX_VSP),
            self._normalize(float(player.get("has_walljump", 0.0)), FALSE, TRUE),
            self._normalize(float(player.get("has_airdodge", 0.0)), FALSE, TRUE),
            self._normalize(float(player.get("djumps", 0.0)), 0.0, MAX_DJUMPS),
            self._normalize(float(player.get("attack_invince", 0.0)), 0.0, MAX_ATTACK_INVINCE),
            self._normalize(float(player.get("respawn_invince_time", 0.0)), 0.0, MAX_RESPAWN_INVINCE_TIME),
            self._normalize(float(player.get("hitstop", 0.0)), 0.0, MAX_HITSTOP),
            self._normalize(float(player.get("hitstop_full", 0.0)), 0.0, MAX_HITSTOP_FULL),
            self._normalize(float(player.get("strong_charge", 0.0)), 0.0, MAX_STRONG_CHARGE),
            self._normalize(float(player.get("window", 0.0)), 0.0, MAX_WINDOW),
            self._normalize(float(player.get("window_timer", 0.0)), 0.0, MAX_WINDOW_TIMER),
            self._normalize(float(player.get("burn_timer", 0.0)), 0.0, MAX_BURN_TIMER),
        ]

    def _get_projectile_values(self, projectile: dict) -> list:
        return [
            self._normalize(float(projectile.get("x", 0.0)), -MAX_X, MAX_X),
            self._normalize(float(projectile.get("y", 0.0)), -MAX_Y, MAX_Y),
            self._normalize(float(projectile.get("hsp", 0.0)), -MAX_HSP, MAX_HSP),
            self._normalize(float(projectile.get("vsp", 0.0)), -MAX_VSP, MAX_VSP),
            self._normalize(float(projectile.get("spr_dir", 0.0)), LEFT, RIGHT),
        ]

    def _get_fire_values(self, fire: dict) -> list:
        return [
            self._normalize(float(fire.get("x", 0.0)), -MAX_X, MAX_X),
            self._normalize(float(fire.get("y", 0.0)), -MAX_Y, MAX_Y),
        ]

    def _get_bubble_values(self, bubble: dict) -> list:
        return [
            self._normalize(float(bubble.get("x", 0.0)), -MAX_X, MAX_X),
            self._normalize(float(bubble.get("y", 0.0)), -MAX_Y, MAX_Y),
            self._normalize(float(bubble.get("hsp", 0.0)), -MAX_HSP, MAX_HSP),
            self._normalize(float(bubble.get("vsp", 0.0)), -MAX_VSP, MAX_VSP),
        ]

    def _get_puddle_values(self, puddle: dict) -> list:
        return [
            self._normalize(float(puddle.get("x", 0.0)), -MAX_X, MAX_X),
            self._normalize(float(puddle.get("y", 0.0)), -MAX_Y, MAX_Y),
        ]
