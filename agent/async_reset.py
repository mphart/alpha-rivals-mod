"""
Keep VecEnv workers stepping while one environment is in a slow menu reset.

SB3's SubprocVecEnv.step() is a barrier: the worker that hits done() calls
env.reset() before returning, so the other games sit on their last action until
that reset finishes. These helpers start reset in a background thread and skip
the in-between menu frames in the PPO rollout buffer.
"""

from __future__ import annotations

import multiprocessing as mp
import threading
import time
from collections.abc import Callable
from typing import Any

import gymnasium as gym
import numpy as np
import torch as th
from gymnasium import spaces
from stable_baselines3 import PPO
from stable_baselines3.common.buffers import RolloutBuffer
from stable_baselines3.common.callbacks import BaseCallback
from stable_baselines3.common.utils import obs_as_tensor
from stable_baselines3.common.vec_env import SubprocVecEnv, VecEnv
from stable_baselines3.common.vec_env.base_vec_env import CloudpickleWrapper
from stable_baselines3.common.vec_env.patch_gym import _patch_env
from stable_baselines3.common.vec_env.subproc_vec_env import _stack_obs

RESET_POLL_SLEEP = 1.0 / 30.0


def _async_reset_worker(
    remote: mp.connection.Connection,
    parent_remote: mp.connection.Connection,
    env_fn_wrapper: CloudpickleWrapper,
) -> None:
    from stable_baselines3.common.env_util import is_wrapped

    parent_remote.close()
    env = _patch_env(env_fn_wrapper.var())
    reset_info: dict[str, Any] = {}
    last_obs = None
    reset_thread: threading.Thread | None = None
    reset_bundle: tuple[Any, dict[str, Any]] | None = None
    resetting = False
    lock = threading.Lock()

    def run_reset(seed=None, options=None) -> None:
        nonlocal reset_bundle
        maybe_options = {"options": options} if options else {}
        obs, info = env.reset(seed=seed, **maybe_options)
        with lock:
            reset_bundle = (obs, info)

    def dummy_obs():
        if last_obs is not None:
            return last_obs
        return np.zeros(env.observation_space.shape, dtype=env.observation_space.dtype)

    while True:
        try:
            cmd, data = remote.recv()
            if cmd == "step":
                if resetting:
                    with lock:
                        bundle = reset_bundle
                    if bundle is None:
                        time.sleep(RESET_POLL_SLEEP)
                        remote.send((dummy_obs(), 0.0, False, {"is_resetting": True}, reset_info))
                    else:
                        observation, reset_info = bundle
                        last_obs = observation
                        resetting = False
                        reset_bundle = None
                        reset_thread = None
                        remote.send((observation, 0.0, False, {"just_reset": True}, reset_info))
                    continue

                observation, reward, terminated, truncated, info = env.step(data)
                done = terminated or truncated
                info["TimeLimit.truncated"] = truncated and not terminated
                last_obs = observation
                if done:
                    info["terminal_observation"] = observation
                    resetting = True
                    reset_bundle = None
                    reset_thread = threading.Thread(target=run_reset, daemon=True)
                    reset_thread.start()
                remote.send((observation, reward, done, info, reset_info))
            elif cmd == "reset":
                if reset_thread is not None:
                    reset_thread.join()
                    reset_thread = None
                with lock:
                    bundle = reset_bundle
                    reset_bundle = None
                resetting = False
                if bundle is not None:
                    observation, reset_info = bundle
                else:
                    maybe_options = {"options": data[1]} if data[1] else {}
                    observation, reset_info = env.reset(seed=data[0], **maybe_options)
                last_obs = observation
                remote.send((observation, reset_info))
            elif cmd == "render":
                remote.send(env.render())
            elif cmd == "close":
                if reset_thread is not None:
                    reset_thread.join(timeout=1.0)
                env.close()
                remote.close()
                break
            elif cmd == "get_spaces":
                remote.send((env.observation_space, env.action_space))
            elif cmd == "env_method":
                method = env.get_wrapper_attr(data[0])
                remote.send(method(*data[1], **data[2]))
            elif cmd == "get_attr":
                remote.send(env.get_wrapper_attr(data))
            elif cmd == "has_attr":
                try:
                    env.get_wrapper_attr(data)
                    remote.send(True)
                except AttributeError:
                    remote.send(False)
            elif cmd == "set_attr":
                remote.send(setattr(env, data[0], data[1]))
            elif cmd == "is_wrapped":
                remote.send(is_wrapped(env, data))
            else:
                raise NotImplementedError(f"`{cmd}` is not implemented in the worker")
        except EOFError:
            break
        except KeyboardInterrupt:
            break


class AsyncResetSubprocVecEnv(SubprocVecEnv):
    """SubprocVecEnv whose workers reset in a background thread instead of blocking step()."""

    def __init__(self, env_fns: list[Callable[[], gym.Env]], start_method: str | None = None):
        self.waiting = False
        self.closed = False
        n_envs = len(env_fns)

        if start_method is None:
            forkserver_available = "forkserver" in mp.get_all_start_methods()
            start_method = "forkserver" if forkserver_available else "spawn"
        ctx = mp.get_context(start_method)

        self.remotes, self.work_remotes = zip(*[ctx.Pipe() for _ in range(n_envs)], strict=True)
        self.processes = []
        for work_remote, remote, env_fn in zip(self.work_remotes, self.remotes, env_fns, strict=True):
            args = (work_remote, remote, CloudpickleWrapper(env_fn))
            process = ctx.Process(target=_async_reset_worker, args=args, daemon=True)
            process.start()
            self.processes.append(process)
            work_remote.close()

        self.remotes[0].send(("get_spaces", None))
        observation_space, action_space = self.remotes[0].recv()
        VecEnv.__init__(self, n_envs, observation_space, action_space)


class IndependentPPO(PPO):
    """
    PPO that keeps stepping live VecEnv workers while others are in menu reset.

    Transitions marked is_resetting / just_reset are not written to the rollout
    buffer. Each env fills its own n_steps of real gameplay independently.
    """

    def collect_rollouts(
        self,
        env: VecEnv,
        callback: BaseCallback,
        rollout_buffer: RolloutBuffer,
        n_rollout_steps: int,
    ) -> bool:
        assert self._last_obs is not None, "No previous observation was provided"
        self.policy.set_training_mode(False)

        n_envs = env.num_envs
        filled = np.zeros(n_envs, dtype=np.int32)
        bootstrap_obs = [None] * n_envs
        bootstrap_dones = np.zeros(n_envs, dtype=bool)

        rollout_buffer.reset()
        if self.use_sde:
            self.policy.reset_noise(env.num_envs)

        callback.on_rollout_start()

        while int(filled.min()) < n_rollout_steps:
            if self.use_sde and self.sde_sample_freq > 0 and int(filled.min()) % self.sde_sample_freq == 0:
                self.policy.reset_noise(env.num_envs)

            with th.no_grad():
                obs_tensor = obs_as_tensor(self._last_obs, self.device)
                actions, values, log_probs = self.policy(obs_tensor)
            actions = actions.cpu().numpy()

            clipped_actions = actions
            if isinstance(self.action_space, spaces.Box):
                if self.policy.squash_output:
                    clipped_actions = self.policy.unscale_action(clipped_actions)
                else:
                    clipped_actions = np.clip(actions, self.action_space.low, self.action_space.high)

            new_obs, rewards, dones, infos = env.step(clipped_actions)

            if isinstance(self.action_space, spaces.Discrete):
                actions = actions.reshape(-1, 1)

            values_np = values.clone().cpu().numpy().flatten()
            log_probs_np = log_probs.clone().cpu().numpy().flatten()
            stored_this_step = 0

            for i in range(n_envs):
                info = infos[i]
                if info.get("is_resetting"):
                    continue
                if info.get("just_reset"):
                    self._last_obs[i] = new_obs[i]
                    self._last_episode_starts[i] = True
                    continue

                if (
                    dones[i]
                    and info.get("terminal_observation") is not None
                    and info.get("TimeLimit.truncated", False)
                ):
                    terminal_obs = self.policy.obs_to_tensor(info["terminal_observation"])[0]
                    with th.no_grad():
                        terminal_value = self.policy.predict_values(terminal_obs)[0]
                    rewards[i] += self.gamma * terminal_value.item()

                if filled[i] < n_rollout_steps:
                    t = int(filled[i])
                    rollout_buffer.observations[t, i] = np.array(self._last_obs[i])
                    rollout_buffer.actions[t, i] = np.array(actions[i]).reshape(rollout_buffer.action_dim)
                    rollout_buffer.rewards[t, i] = rewards[i]
                    rollout_buffer.episode_starts[t, i] = self._last_episode_starts[i]
                    rollout_buffer.values[t, i] = values_np[i]
                    rollout_buffer.log_probs[t, i] = log_probs_np[i]
                    filled[i] += 1
                    stored_this_step += 1
                    if filled[i] == n_rollout_steps:
                        bootstrap_obs[i] = np.array(new_obs[i], copy=True)
                        bootstrap_dones[i] = bool(dones[i])

                self._last_obs[i] = new_obs[i]
                self._last_episode_starts[i] = bool(dones[i])

            if stored_this_step:
                self.num_timesteps += stored_this_step
                callback.update_locals(locals())
                if not callback.on_step():
                    return False
                self._update_info_buffer(infos, dones)

        stacked_bootstrap = np.stack(bootstrap_obs)
        with th.no_grad():
            last_values = self.policy.predict_values(obs_as_tensor(stacked_bootstrap, self.device))

        rollout_buffer.pos = n_rollout_steps
        rollout_buffer.full = True
        rollout_buffer.compute_returns_and_advantage(last_values=last_values, dones=bootstrap_dones)

        callback.update_locals(locals())
        callback.on_rollout_end()
        return True
