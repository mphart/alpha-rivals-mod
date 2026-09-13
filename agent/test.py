"""
Test script for the new list_instances command.
 
Run this while a match is in progress, ideally with a projectile active,
to see the full list of live GameMaker instances and their object_index
values. Snapshot known players and known projectiles to figure out which
numeric object_index values correspond to which object type.
"""
 
import json
from bridge import Bridge
 
bridge = Bridge()
 
raw = bridge.send("list_instances")
data = json.loads(raw)
 
instances = data.get("instances", [])
print(f"Found {len(instances)} active instance(s):\n")
 
for inst in instances:
    print(f"  addr={inst['addr']:>10}  object_index={inst['object_index']:>6}  "
          f"sprite_index={inst['sprite_index']}")
 
