"""
PPO training entry point for the RoA environment.
Uses self-play to train a single agent against a static opponent.

Usage:
    1. launch N_ENVS instances of Rivals of Aether
    2. attach alpha-rivals-mod to each instance
    3. in each instance, get to the character select screen
    4. run this script

Each subprocess claims a different injected game via the per-PID named pipe.
N_ENVS must not exceed the number of injected game instances.
"""

from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.callbacks import CheckpointCallback
from torch import nn
from async_reset import AsyncResetSubprocVecEnv, IndependentPPO
from env import RoAEnv

# Set this to a checkpoint path (e.g. "./checkpoints/ppo_roa_20000_steps")
# to resume training from it. Leave as None to start fresh.
RESUME_FROM_CHECKPOINT =  None

TOTAL_TIMESTEPS = 100_000_000
N_ENVS = 3  # one subprocess per injected game instance
CHECKPOINT_EVERY = 100_000  # timesteps between checkpoint files


def main():
    env = make_vec_env(
        RoAEnv,
        n_envs=N_ENVS,
        vec_env_cls=AsyncResetSubprocVecEnv,
        vec_env_kwargs={"start_method": "spawn"},
    )

    kwargs = dict(
        net_arch=[512, 256, 256, 128],
        activation_fn=nn.ReLU,
        ortho_init=True,
    )

    if RESUME_FROM_CHECKPOINT:
        print(f"Resuming from checkpoint: {RESUME_FROM_CHECKPOINT}")
        model = IndependentPPO.load(RESUME_FROM_CHECKPOINT, env=env)
    else:
        model = IndependentPPO(
            policy="MlpPolicy",
            env=env,
            n_steps=4000,           # rollout length per env before each PPO update
            batch_size=500,
            n_epochs=12,
            gamma=0.99,             # discount factor
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,         
            learning_rate=3e-4,
            policy_kwargs=kwargs, 
            verbose=1,
            tensorboard_log="./selfplay_roa_tensorboard/",
        )

    # CheckpointCallback counts vec-env steps, each of which is N_ENVS timesteps.
    checkpoint_callback = CheckpointCallback(
        save_freq=max(CHECKPOINT_EVERY // N_ENVS, 1),
        save_path="./checkpoints/",
        name_prefix="selfplay_zetter_orcane_1v1",
    )

    model.learn(
        total_timesteps=TOTAL_TIMESTEPS,
        callback=checkpoint_callback,
        progress_bar=True,
        reset_num_timesteps=(RESUME_FROM_CHECKPOINT is None),
    )

    model.save("selfplay_zetter_orcane")


if __name__ == "__main__":
    main()