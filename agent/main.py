import win32file
import json

pipe = win32file.CreateFile(
    r'\\.\pipe\bridge',
    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
    0, None, win32file.OPEN_EXISTING, 0, None
)

win32file.WriteFile(pipe, b"get_state")
result = win32file.ReadFile(pipe, 4096)
data = json.loads(result[1])
print(json.dumps(data, indent=2))