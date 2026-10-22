# GTA San Andreas MCP Command Reference

Complete guide for agents and developers interacting with GTA San Andreas through MCP.

---

## 1. Quick Enum Lookup Tool

Use `sdk_lookup` at any time to find exact numeric IDs and constants:
```json
{
  "category": "weapons" | "vehicles" | "weather" | "stats" | "radio" | "pedTypes" | "tasks" | "tuning" | "cheats" | "commonOpcodes" | "all",
  "query": "m4" // optional filter
}
```

---

## 2. Tool Categories

### 🎮 Game & Process Control
| Tool | Description | Parameters |
|---|---|---|
| `game_launch` | Launch game with MCP debug bridge active | `{}` |
| `game_status` | Check if game process and pipe bridge are active | `{}` |
| `game_state` | Read current scene (`loading`, `menu`, `paused`, `cutscene`, `in_game`) | `{}` |
| `game_kill` | Terminate GTA SA process and clean lockfile | `{}` |
| `game_cheat` | Trigger in-game cheats (`health_armour_money`, `weapon1`, `weapon2`, `weapon3`, `jetpack`, `never_wanted`, `six_stars`, `infinite_ammo`, `infinite_health`, `blow_up_cars`, `cars_fly`, `cars_on_water`, `max_stamina`, `max_muscle`, `max_driving`, `hitman_weapons`) | `cheat: string` |
| `key_tap` | Simulate Windows keyboard key press for N frames | `keyCode: int, frames?: int` |
| `screenshot` | Capture current GTA SA window to PNG file | `{}` |

---

### 🏃 Player (CJ)
| Tool | Description | Parameters |
|---|---|---|
| `player_get` | Get CJ position, heading, health, armour, wanted, money, interior, vehicle handle | `{}` |
| `player_set` | Update CJ attributes (health, maxHealth, armour, wanted, money, god mode) | `health?: float, maxHealth?: float, armour?: float, wanted?: int, money?: int, invincible?: bool` |
| `player_teleport` | Instant coordinates teleport | `x: float, y: float, z: float, heading?: float` |
| `player_enter_vehicle` | Walk and enter vehicle as driver | `vehicleHandle: int` |
| `player_give_weapon` | Give weapon with ammo | `weaponType: int, ammo: int, setActive?: bool` |
| `player_remove_weapon`| Remove specific weapon | `weaponType: int` |
| `player_clear_weapons` | Clear all weapons | `{}` |
| `player_set_skin` | Change CJ ped model | `modelId: int` |
| `player_set_stat` | Set player stat value (fat, muscle, stamina, skills) | `statId: int, value: float` |
| `player_get_stat` | Read player stat value | `statId: int` |
| `player_clear_wanted` | Reset wanted level to 0 | `{}` |

---

### 🚗 Vehicles
| Tool | Description | Parameters |
|---|---|---|
| `vehicle_spawn` | Spawn any vehicle (car, bike, bmx, plane, heli, boat, train, tank) | `modelId: int, offsetX?: float, offsetY?: float` |
| `vehicle_get` | Inspect vehicle state (speed, health, gear, lock, colors) | `handle?: int` |
| `vehicle_set` | Modify vehicle properties | `handle?: int, health?: float, doorLock?: int, engine?: bool, dirtLevel?: float, alarm?: int` |
| `vehicle_set_color` | Set primary & secondary body colors | `handle?: int, primaryColor: int, secondaryColor?: int` |
| `vehicle_tune` | Install tuning components (nitro, hydraulics, wheels, spoilers) | `handle?: int, upgradeModelId: int` |
| `vehicle_pop_tire` | Burst specific wheel/tire | `handle?: int, tireIndex: 0..3` |
| `vehicle_set_door_state` | Open/close doors (hood, trunk, FL, FR, RL, RR) | `handle?: int, doorId: 0..5, ratio?: 0.0..1.0` |
| `vehicle_teleport` | Move vehicle to coordinates | `handle?: int, x: float, y: float, z: float` |
| `vehicle_fix` | Fully repair and unflip vehicle | `handle?: int` |
| `vehicle_remove` | Delete vehicle from world | `handle?: int` |

---

### 🚶 Peds & NPCs
| Tool | Description | Parameters |
|---|---|---|
| `ped_spawn` | Spawn civilian/cop/gang ped | `modelId: int, offsetX?: float, offsetY?: float, pedType?: int` |
| `ped_get` | Inspect ped state & alive status | `handle: int` |
| `ped_set` | Update ped health/armour | `handle: int, health?: float, armour?: float` |
| `ped_set_task` | Assign AI behavior (`wander`, `follow_player`, `attack_player`, `flee_player`, `hands_up`, `duck`, `jetpack`, `enter_vehicle`, `leave_vehicle`, `drive_to`, `play_anim`, `die`) | `handle: int, task: string, ...` |
| `ped_clear_tasks` | Clear ped AI tasks immediately | `handle: int` |
| `ped_give_weapon` | Equip ped with weapon & ammo | `handle: int, weaponType: int, ammo?: int, setActive?: bool` |
| `ped_teleport` | Move ped to world coords | `handle: int, x: float, y: float, z: float` |
| `ped_resurrect` | Revive dead ped to full health | `handle: int` |
| `ped_remove` | Delete ped from world | `handle: int` |

---

### 🎥 Camera
| Tool | Description | Parameters |
|---|---|---|
| `camera_get` | Get camera position, front vector, FOV, mode | `{}` |
| `camera_set_pos` | Place fixed camera at coordinates pointing at target | `x: float, y: float, z: float, targetX?: float, targetY?: float, targetZ?: float` |
| `camera_point_at` | Point camera at ped/vehicle handle or coordinate | `pedHandle?: int, vehicleHandle?: int, x?: float, y?: float, z?: float` |
| `camera_restore` | Return camera to normal 3rd-person follow | `{}` |
| `camera_fade` | Fade screen in/out | `duration: float, direction: "in" | "out"` |
| `camera_shake` | Apply screen shake effect | `intensity?: float` |
| `camera_set_mode` | Set camera mode ID | `mode: int` |

---

### 🌍 World & Environment
| Tool | Description | Parameters |
|---|---|---|
| `world_set_time` | Set in-game hour and minute | `hour: 0..23, minute?: 0..59` |
| `world_get_time` | Read in-game time | `{}` |
| `world_set_weather` | Set weather ID (sunny, rainy, foggy, sandstorm) | `weatherId: 0..45` |
| `world_set_timescale` | Slow-motion or fast-motion (0=freeze, 1=normal) | `scale: float` |
| `world_set_gravity` | Modify global gravity (default 0.008) | `gravity: float` |
| `world_set_traffic_density` | Traffic spawn density multiplier | `multiplier: float` |
| `world_set_ped_density` | Pedestrian spawn density multiplier | `multiplier: float` |
| `world_explosion` | Create explosion at coordinates | `x: float, y: float, z: float, type?: int` |
| `world_create_object` | Spawn map object/prop (barrels, ramps, crates) | `modelId: int, x: float, y: float, z: float` |
| `world_remove_object` | Remove map object | `handle: int` |
| `world_create_pickup` | Spawn weapon, health, armor, or money pickup | `x: float, y: float, z: float, modelId?: int, weaponType?: int, ammo?: int, pickupType?: int` |
| `world_remove_pickup` | Remove pickup | `handle: int` |
| `world_create_fire` | Spawn fire at coordinates | `x: float, y: float, z: float, size?: float` |
| `world_extinguish_fires` | Extinguish fires in area | `x: float, y: float, z: float, radius?: float` |
| `world_pool_dump` | Query all nearby peds and vehicles in radius | `radius?: float` |
| `radar_get_blips` | List active blips on map & radar | `{}` |
| `radar_add_blip` | Add marker/blip to map | `x: float, y: float, z: float, color?: int, spriteId?: int` |
| `radar_remove_blip` | Remove blip | `blipIndex: int` |

---

### 🔊 Audio & Radio
| Tool | Description | Parameters |
|---|---|---|
| `audio_play_sound` | Play 2D frontend sound effect | `soundId: int, volume?: float` |
| `audio_set_radio` | Change active radio station (0=Playback FM, 2=K-DST, 5=Radio Los Santos, 6=Radio X, 12=Off) | `stationId: 0..12` |

---

### ⚡ Universal Script Opcode Runner
| Tool | Description | Parameters |
|---|---|---|
| `script_call_opcode` | Call ANY GTA SA SCM script command dynamically | `opcode: int, types?: string, values?: string` |

*Example:*
```json
{
  "opcode": 263, // 0x0107 SET_WEATHER
  "types": "int",
  "values": "8" // rainy SF
}
```

---

### 🧩 Plugin Hot Reload & Lifecycle
| Tool | Description | Parameters |
|---|---|---|
| `plugin_load` | Dynamically load an `.asi` / `.dll` plugin at runtime | `path: string` |
| `plugin_unload` | Safely unload an `.asi` plugin from memory | `pluginName: string` |
| `plugin_reload` | Hot reload plugin after rebuilding (frees old DLL, loads fresh build) | `pluginName?: string, path?: string` |
| `plugin_log_read` | Read exported log buffer from ASI plugin (`GtaMcpDebugLog` / `GtaMcpDebugLen`) | `pluginName: string, clear?: bool` |

#### Safe Plugin Hot Reload Pattern:
When developing a plugin that you intend to hot reload:
1. Store event references and unregister them in destructor / `DLL_PROCESS_DETACH`:
   ```cpp
   Events::gameProcessEvent -= myHandler;
   ```
2. Revert memory patches or use RAII hooks (`safetyhook` or `patch::NopRestore`).
3. Call `plugin_reload({ pluginName: "myplugin.asi", path: "C:/path/to/myplugin.asi" })`.

---

### 🧠 Memory & Debugging
| Tool | Description | Parameters |
|---|---|---|
| `memory_read` | Read memory bytes (`hex`, `int32`, `float`, `string`) | `address: int, size: int, type?: string` |
| `memory_write` | Write value (`int32`, `float`, `bytes`) | `address: int, type: string, value: any` |
| `memory_nop` | NOP memory range | `address: int, size?: int` |
