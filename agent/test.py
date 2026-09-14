"""
Test script for list_instances / dump_vars.

Run this while a match is in progress. dump_vars walks the custom GML
variable hashmap on the first live instance of a given object_index
(3=oPlayer, 5=pHurtBox, 6=pHitBox, 17=ground fire).
"""

import json
from bridge import Bridge

bridge = Bridge()

raw = bridge.send("list_instances")
data = json.loads(raw)

# print(json.loads(bridge.send("get_state")))

print("=== dump_css ===")
css = json.loads(bridge.send("dump_css"))
print(json.dumps(css, indent=2))
print("=== choices ===")
print(bridge.send("get_player_choice 0"))
print(bridge.send("get_player_choice 1"))
print(bridge.send("get_player_choice 2"))
print(bridge.send("get_player_choice 3"))

print(bridge.send("set_player_choice 0 3"))
print(bridge.send("set_player_choice 1 2"))
print(bridge.send("set_player_choice 2 1"))
print(bridge.send("set_player_choice 3 3"))

# instances = data.get("instances", [])
# print(f"hsp_index={data.get('hsp_index')}  vsp_index={data.get('vsp_index')}")
# print(f"Found {len(instances)} active instance(s):\n")

# for inst in instances:
#     print(f"  addr={inst['addr']:>10}  id={inst.get('id', '?'):>6}  "
#           f"object_index={inst['object_index']:>6}  name={inst.get('name', ''):<22}  "
#           f"sprite_index={inst['sprite_index']:>5}  "
#           f"x={inst.get('x', 0):8.2f}  y={inst.get('y', 0):8.2f}  "
#           f"hsp={inst.get('hsp', '-')}  vsp={inst.get('vsp', '-')}")

# for oid in (3, 6):
#     raw = bridge.send(f"dump_vars {oid}")
#     dump = json.loads(raw)
#     vars_ = dump.get("vars", [])
#     print(f"\n=== dump_vars {oid}  name={dump.get('name')}  "
#           f"addr={dump.get('addr')}  packed={dump.get('packed')}  "
#           f"yyvars={dump.get('yyvars')}  n30={dump.get('n30')}  "
#           f"map_used={dump.get('map_used')}/{dump.get('map_cap')}  "
#           f"nvars={len(vars_)} ===")
#     if not vars_:
#         print("  (no custom vars)")
#         continue
#     interesting = []
#     for v in vars_:
#         name = (v.get("name") or "").lower()
#         if any(k in name for k in ("hsp", "vsp", "percent", "char", "state", "spr")):
#             interesting.append(v)
#     print(f"  interesting ({len(interesting)}):")
#     for v in interesting:
#         extra = v["v"] if "v" in v else v.get("ptr", "")
#         print(f"    i={v.get('i'):5}  kind={v.get('kind'):2}  "
#               f"name={v.get('name', ''):<28}  {extra}")
#     print("  all reals:")
#     for v in vars_:
#         if v.get("kind") == 0 and "v" in v:
#             print(f"    i={v.get('i'):5}  name={v.get('name', ''):<28}  {v['v']}")
