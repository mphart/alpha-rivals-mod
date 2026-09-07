"""
PPO training entry point for the RoA environment.

Prerequisites:
    pip install stable-baselines3 gymnasium

Usage:
    1. Launch Rivals of Aether, inject the DLL (as you've been doing).
    2. Get into a match manually (vs CPU, player 1 = your XInput-controlled
       slot) since match_reset() in roa_env.py is currently a stub.
    3. Run this script.

This currently trains a single agent (player 1) against RoA's built-in
CPU opponent (player 2), rather than self-play, to keep the setup simple
while validating the sparse reward signal. Self-play can be layered on
later once this loop is confirmed to learn *something*.
"""

from stable_baselines3 import PPO
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.callbacks import CheckpointCallback

from env import RoAEnv

# Set this to a checkpoint path (e.g. "./checkpoints/ppo_roa_20000_steps.zip")
# to resume training from it. Leave as None to start fresh.
RESUME_FROM_CHECKPOINT = None

TOTAL_TIMESTEPS = 1_000_000
FPS = 30


def main():
    env = make_vec_env(lambda: RoAEnv(), n_envs=1)

    if RESUME_FROM_CHECKPOINT:
        print(f"Resuming from checkpoint: {RESUME_FROM_CHECKPOINT}")
        model = PPO.load(RESUME_FROM_CHECKPOINT, env=env)
    else:
        model = PPO(
            policy="MlpPolicy",
            env=env,
            n_steps=2048,           # rollout length before each PPO update
            batch_size=256,
            n_epochs=10,
            gamma=0.9999,            # discount factor
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,         
            learning_rate=3e-4,
            policy_kwargs=dict(net_arch=[512, 512, 512, 512, 512, 512, 512, 512]),  # matches our earlier sizing discussion
            verbose=1,
            tensorboard_log="./ppo_roa_tensorboard/",
        )

    checkpoint_callback = CheckpointCallback(
        save_freq=25000,
        save_path="./checkpoints/",
        name_prefix="ppo_zetter_1v1",
    )

    model.learn(
        total_timesteps=1_000_000,  
        callback=checkpoint_callback,
        progress_bar=True,
        reset_num_timesteps=(RESUME_FROM_CHECKPOINT is None),
    )

    model.save("ppo_roa_final")


if __name__ == "__main__":
    main()