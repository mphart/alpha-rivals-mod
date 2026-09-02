from main import Bridge
import time

bridge = Bridge()


bridge.set_joy_button(0, 'a', True)
time.sleep(0.3)
bridge.set_joy_button(0, 'a', False)
time.sleep(0.3)
bridge.set_joy_button(1, 'a', True)
time.sleep(0.3)
bridge.set_joy_button(1, 'a', False)
time.sleep(2)
bridge.set_joy_button(1, 'b', True)
time.sleep(0.3)
bridge.set_joy_button(1, 'b', False)
time.sleep(0.3)
bridge.set_joy_button(0, 'a', True)