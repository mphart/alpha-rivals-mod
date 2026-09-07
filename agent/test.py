from bridge import Bridge
import time

bridge = Bridge()


if __name__ == "__main__":
    # print(bridge.send('get_bases'))
    print(bridge.send('get_state'))