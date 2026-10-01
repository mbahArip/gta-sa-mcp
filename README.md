# gta-sa-mcp

MCP server for debugging GTA San Andreas plugins. Launches the game, connects via a named pipe to a companion ASI, and exposes tools for reading/controlling game state.

## Requirements

- [Bun](https://bun.sh) ≥ 1.0
- Visual Studio 2022 (to build `debug_bridge.asi`)
- GTA SA 1.0 US (Compact or Hoodlum)
- [ASI Loader](https://www.gtagarage.com/mods/show.php?id=21709) installed in the game directory
- [plugin-sdk-sa](https://github.com/DK22Pac/plugin-sdk) cloned at `../plugin-sdk-sa` (sibling directory)

## Setup

### 1. Build debug_bridge.asi

Open `bridge/debug_bridge.vcxproj` in Visual Studio 2022 and build **Release | Win32**.

Copy the output `debug_bridge.asi` to your GTA SA directory (same folder as `gta_sa.exe`).

### 2. Install MCP server dependencies

```bash
bun install
```

### 3. Configure in Antigravity (or any MCP client)

```json
{
  "mcpServers": {
    "gtasa": {
      "command": "bun",
      "args": [
        "run",
        "C:/Users/me/dev/sa_project/gta-sa-mcp/src/index.ts",
        "--gamePath=C:/Program Files/Rockstar Games/GTA San Andreas"
      ]
    }
  }
}
```

**Config priority:** `--gamePath` CLI arg → `GTASA_PATH` env → Windows registry → error.

If GTA SA is installed to the default Steam/Rockstar location, registry detection works automatically (no arg needed).

## How it works

1. **`game_launch`** creates `mcp_debug.lock` in the game directory, then spawns `gta_sa.exe`
2. **`debug_bridge.asi`** checks for `mcp_debug.lock` on DLL load — if present, opens `\\.\pipe\gtasa_mcp_bridge` and starts processing commands each frame
3. MCP tools send JSON commands over the pipe; bridge executes them on the game thread and returns results
4. **`game_kill`** kills the process and deletes `mcp_debug.lock`

If the game is already running **without** the sentinel file, `debug_bridge.asi` is dormant (zero overhead). Only game sessions started via `game_launch` have the bridge active.

## Tools

| Tool | Description |
|---|---|
| `game_launch` | Start GTA SA with debug bridge |
| `game_status` | Check if game is running and bridge is connected |
| `game_kill` | Kill game, remove sentinel |
| `player_get` | CJ position, health, armour, wanted, money, interior |
| `player_set` | Set health, armour, wanted level, money |
| `player_teleport` | Teleport CJ to world coordinates |
| `player_give_weapon` | Give weapon + ammo |
| `vehicle_spawn` | Spawn vehicle by model ID near CJ |
| `vehicle_get` | Vehicle health, speed, gear, door lock |
| `vehicle_set` | Set vehicle health, door lock, engine |
| `ped_spawn` | Spawn NPC by model ID |
| `ped_get` | NPC position, health, model |
| `ped_set` | Set NPC health/armour |
| `ped_set_task` | Assign task: wander/follow_player/attack_player/flee_player/hands_up/die |
| `world_get_time` | Get in-game clock |
| `world_set_time` | Set in-game clock |
| `world_set_weather` | Set weather ID |
| `world_pool_dump` | List all peds/vehicles within a radius |
| `memory_read` | Read bytes at a game address (hex/int32/float/string) |
| `memory_write` | Write int32/float/bytes at a game address |
| `plugin_log_read` | Read debug log exported by your .asi plugin |

## Plugin debug logging

Add 2 lines to your plugin to enable `plugin_log_read`:

```cpp
extern "C" __declspec(dllexport) char g_mcpLog[4096]  = {};
extern "C" __declspec(dllexport) int  g_mcpDebugLen    = 0;

// Write to it anywhere:
g_mcpDebugLen += sprintf_s(g_mcpLog + g_mcpDebugLen, 4096 - g_mcpDebugLen,
    "[frame] vehicle health: %.1f\n", veh->m_fHealth);
```

Then from the MCP:
```
plugin_log_read("myplugin.asi", clear=true)
```

## Environment variables

| Variable | Default | Description |
|---|---|---|
| `GTASA_PATH` | (registry) | Game directory path |
| `GTASA_PIPE_TIMEOUT` | `5000` | Pipe connect timeout (ms) |
| `GTASA_LAUNCH_WAIT` | `15000` | Max wait for game to start (ms) |
