"""
PPO training entry point for the RoA environment.
Uses self-play to train a single agent against a static opponent.

Usage:
    1. launch Rivals of Aether
    2. attach alpha-rivals-mod
    3. join a match with two controllers, then get to the post-game screen
    4. run this script
"""

from stable_baselines3 import PPO
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.callbacks import CheckpointCallback

from env import RoAEnv

# Set this to a checkpoint path (e.g. "./checkpoints/ppo_roa_20000_steps.zip")
# to resume training from it. Leave as None to start fresh.
RESUME_FROM_CHECKPOINT = './checkpoints/selfplay_zetter_1v1_150000_steps.zip'

TOTAL_TIMESTEPS = 1_000_000


def main():
    env = make_vec_env(lambda: RoAEnv(), n_envs=1)

    if RESUME_FROM_CHECKPOINT:
        print(f"Resuming from checkpoint: {RESUME_FROM_CHECKPOINT}")
        model = PPO.load(RESUME_FROM_CHECKPOINT, env=env)
    else:
        model = PPO(
            policy="MlpPolicy",
            env=env,
            n_steps=2000,           # rollout length before each PPO update
            batch_size=250,
            n_epochs=10,
            gamma=0.99,             # discount factor
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,         
            learning_rate=3e-4,
            policy_kwargs=dict(net_arch=[256, 256, 256]),  # matches our earlier sizing discussion
            verbose=1,
            tensorboard_log="./selfplay_roa_tensorboard/",
        )

    checkpoint_callback = CheckpointCallback(
        save_freq=25_000,
        save_path="./checkpoints/",
        name_prefix="selfplay_zetter_1v1",
    )

    model.learn(
        total_timesteps=1_000_000,  
        callback=checkpoint_callback,
        progress_bar=True,
        reset_num_timesteps=(RESUME_FROM_CHECKPOINT is None),
    )

    model.save("selfplay_roa_zetter_1v1_v1")


if __name__ == "__main__":
    main()