from main import Bridge
import time

bridge = Bridge()


VK_X = 0x58  # or whichever key you confirmed controls an action

bridge.set_key(VK_X, False)
time.sleep(0.3)
print(bridge.set_key(VK_X, True))
time.sleep(3)
print(bridge.set_key(VK_X, False))