# gta-sa-mcp

MCP server that lets a coding agent **drive a live GTA San Andreas process** — read game state, spawn stuff, run any SCM opcode, poke memory, and hot-reload the plugin you're building without restarting the game.

Built for **GTA SA 1.0 US** plugin development. 73 tools, one named pipe, zero game restarts.

---

## What it's for

You're writing a `.asi` plugin with [plugin-sdk](https://github.com/DK22Pac/plugin-sdk). Normally every test cycle is: build → copy `.asi` → restart game → load save → drive to a spot → check. That's minutes per iteration.

With this, the agent does:

```
game_launch                                    # boots + skips intro automatically
plugin_reload({pluginName:"myplugin.asi", path:"C:/build/myplugin.asi"})
plugin_log_read({pluginName:"myplugin.asi"})
screenshot
```

…and sees the result. Iteration is seconds.

Concrete uses:

| Use | How |
|---|---|
| **Plugin dev loop** | `plugin_load` / `plugin_reload` / `plugin_unload` / `plugin_log_read` — rebuild and reload without exiting |
| **Verify behaviour** | `screenshot`, `player_get`, `vehicle_get`, `ped_get`, `world_pool_dump` — assert on real state, not guesses |
| **Set up a scenario** | `player_teleport`, `vehicle_spawn`, `ped_spawn`, `world_set_time`, `world_set_weather` — put the world in the exact state the bug needs |
| **Test hotkeys** | `key_tap` — inject VK codes for N frames, no physical keyboard |
| **Reverse engineering** | `memory_read` / `memory_write` / `memory_nop` at any address |
| **Call anything** | `script_call_opcode` — all ~2000 GTA SA SCM commands, plus `sdk_lookup` for the IDs |
| **Cinematic shots** | `camera_set_pos`, `camera_point_at`, `camera_fade`, `camera_shake`, `world_set_timescale` |

Not for: playing the game, speedrunning, or Definitive Edition (classic 1.0 US only).

---

## Install

### 1. Requirements

- **GTA San Andreas 1.0 US** (Compact or Hoodlum exe)
- **ASI Loader** in the game folder — [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
- **[Bun](https://bun.sh) ≥ 1.0**
- Windows (the bridge is Win32-only)

### 2. Drop in the bridge

Download `debug_bridge.asi` from the [latest release](../../releases/latest) and copy it next to `gta_sa.exe`.

The bridge is **dormant unless `mcp_debug.lock` exists** in the game directory — if you launch the game normally, it does nothing and costs nothing.

### 3. Get the server

```bash
git clone https://github.com/mbahArip/gta-sa-mcp.git
cd gta-sa-mcp
bun install
```

### 4. Find your game path

Resolution order: `--gamePath=` arg → `GTASA_PATH` env → Windows registry → error. If GTA SA is in the default Rockstar/Steam location, the registry works and you can skip this.

---

## Use with a coding agent

The server speaks stdio MCP. Point any MCP client at it.

### opencode / jcode (`~/.config/opencode/opencode.json`)

```json
{
  "mcp": {
    "gta-sa-mcp": {
      "type": "local",
      "enabled": true,
      "command": ["bun", "run", "C:/path/to/gta-sa-mcp/src/index.ts"],
      "environment": {
        "GTASA_PATH": "D:/Games/gta sa/GTA San Andreas"
      }
    }
  }
}
```

### Claude Desktop / Cursor / Antigravity (`mcpServers` shape)

```json
{
  "mcpServers": {
    "gta-sa-mcp": {
      "command": "bun",
      "args": ["run", "C:/path/to/gta-sa-mcp/src/index.ts"],
      "env": {
        "GTASA_PATH": "D:/Games/gta sa/GTA San Andreas"
      }
    }
  }
}
```

Use forward slashes in paths. Restart the client after editing.

### First session

```
game_launch
```

Launches the game, creates `mcp_debug.lock`, waits for the pipe. The bridge auto-skips the intro cutscene so you land in gameplay.

Then confirm:

```
game_status      # { running, bridgeConnected, state }
game_state       # { scene: "in_game", ... }
```

### The one rule

**Gameplay commands only execute when `scene === "in_game"`.** During loading, menu, pause, or a cutscene they queue and expire after 15 seconds with:

```
queued command expired waiting for gameplay (15s)
```

That is not a bug — check `game_state` first. `game_state` itself always answers immediately (served from a 100 ms cache).

---

## Tool reference

73 tools across 11 modules. Full parameter docs: [`docs/COMMANDS.md`](docs/COMMANDS.md).

| Module | # | Tools |
|---|---|---|
| `game.ts` | 7 | `game_launch` `game_status` `game_state` `game_kill` `game_cheat` `key_tap` `screenshot` |
| `player.ts` | 11 | `player_get` `player_set` `player_teleport` `player_enter_vehicle` `player_give_weapon` `player_remove_weapon` `player_clear_weapons` `player_set_skin` `player_set_stat` `player_get_stat` `player_clear_wanted` |
| `vehicle.ts` | 10 | `vehicle_spawn` `vehicle_get` `vehicle_set` `vehicle_set_color` `vehicle_tune` `vehicle_pop_tire` `vehicle_set_door_state` `vehicle_teleport` `vehicle_fix` `vehicle_remove` |
| `ped.ts` | 9 | `ped_spawn` `ped_get` `ped_set` `ped_set_task` `ped_clear_tasks` `ped_give_weapon` `ped_teleport` `ped_resurrect` `ped_remove` |
| `world.ts` | 18 | time, weather, timescale, gravity, traffic/ped density, explosions, objects, pickups, fire, pool dump, radar blips |
| `camera.ts` | 7 | `camera_get` `camera_set_pos` `camera_point_at` `camera_restore` `camera_fade` `camera_shake` `camera_set_mode` |
| `audio.ts` | 2 | `audio_play_sound` `audio_set_radio` |
| `plugin.ts` | 4 | `plugin_load` `plugin_unload` `plugin_reload` `plugin_log_read` |
| `memory.ts` | 3 | `memory_read` `memory_write` `memory_nop` |
| `script.ts` | 1 | `script_call_opcode` — any SCM opcode |
| `helper.ts` | 1 | `sdk_lookup` — weapons / vehicles / weather / stats / radio / pedTypes / tasks / tuning / cheats / commonOpcodes |

### Escaping the tool surface

Anything not wrapped is still reachable:

```
sdk_lookup({ category: "vehicles", query: "infernus" })   # → 411
script_call_opcode({ opcode: 0x0107, types: "int", values: "8" })   # SET_WEATHER rainy
```

`script_call_opcode` runs raw SCM commands, so the whole game script API is in reach without waiting for a tool wrapper.

---

## Plugin development

### Loop

```
game_state                                              # must be in_game
plugin_unload({ pluginName: "myplugin.asi" })
# msbuild /p:Configuration=Release /p:Platform=Win32
plugin_load({ path: "myplugin.asi" })
plugin_log_read({ pluginName: "myplugin.asi", clear: true })
screenshot
```

Or, when the build lands somewhere else and the module name stays stable:

```
plugin_reload({ pluginName: "myplugin.asi", path: "C:/build/myplugin.asi" })
```

### Hot reload gotchas

- **File lock.** Windows locks a loaded DLL against writes. `plugin_reload` to the *same* path can't work if the build failed mid-write — unload first, build, then load. Building to a scratch dir and reloading from there avoids the issue entirely.
- **No rollback.** Unload happens before load. A broken build leaves the feature gone, not the old version running. Read the error: `126` = missing dependency, `193` = wrong architecture (must be **Win32 x86**), `998` = bad path.
- **Verify it actually reloaded.** If your refcount is >1, `FreeLibrary` drops a count and `LoadLibraryA` returns the *existing* module — stale code, silently. Compare the returned `handle` to the previous one.
- **Freeing a hooked plugin crashes.** Revert every `injector::` patch and unregister every `Events::` handler before unload. `debug_bridge.asi` cannot protect you from a jump into freed memory.
- **Bridge can't reload itself.** Restart the game to update `debug_bridge.asi`.

### Plugin-side requirements

```cpp
// 1. Log buffer for plugin_log_read
extern "C" __declspec(dllexport) char GtaMcpDebugLog[4096] = {};
extern "C" __declspec(dllexport) int  GtaMcpDebugLen = 0;

// 2. Unload export — bridge calls this before FreeLibrary
extern "C" __declspec(dllexport) void GtaMcpUnload() {
    Events::gameProcessEvent -= myHandler;
    Events::drawingEvent     -= myDraw;
    patch::NopRestore(/* ... */);
}
```

The bridge also accepts `PluginUnload` and `OnUnload` as fallback names. Log writes go through the exported buffer, so `GtaMcpDebugLen` must track the byte count.

Log is capped at 4096 bytes; `clear: true` zeroes it.

---

## Build the bridge from source

Needs **Visual Studio 2022** (v145 toolset) and a [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) checkout.

```bash
# plugin-sdk must be a sibling of this repo, or point the env var at it
git clone https://github.com/DK22Pac/plugin-sdk ../plugin-sdk-sa
```

```bash
# sibling layout (default)
msbuild bridge/debug_bridge.vcxproj /p:Configuration=Release /p:Platform=Win32

# custom plugin-sdk location
set PLUGIN_SDK_ROOT=C:\dev\plugin-sdk
msbuild bridge/debug_bridge.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Output: `bridge/Release/debug_bridge.asi`. Copy it to the game folder.

`Platform=Win32` is mandatory — GTA SA is 32-bit. An x64 build fails to load with error 193.

### Debugging without the MCP layer

```bash
bun tools/pipe-test.ts game_state
bun tools/pipe-test.ts vehicle_spawn modelId=477 offsetX=0
bun tools/pipe-test.ts player_teleport x=2495 y=-1687 z=13
```

Raw pipe client, no MCP. Useful when the server itself is suspect.

---

## Architecture

```
agent ──stdio MCP──> src/index.ts ──named pipe──> debug_bridge.asi ──> GTA SA
                     (Bun/TS)      \\.\pipe\gtasa_mcp_bridge   (in-process, game thread)
```

- `src/pipe.ts` — pipe client, newline-delimited JSON, 20 s default timeout, 100 ms `game_state` cache
- `src/process.ts` — launch/detect/kill, `mcp_debug.lock` sentinel
- `src/config.ts` — path resolution
- `src/tools/*.ts` — one file per domain, each exports a `register*Tools(server)`
- `bridge/Main.cpp` — pipe server thread + all command handlers

**Threading.** Commands arrive on a pipe thread, get queued, and are drained inside `Events::gameProcessEvent` on the game thread — so handlers can touch game memory safely. Only `game_state` is answered off the pipe thread (it reads nothing that needs the game thread), which is why it responds during loading.

**Protocol.**

```jsonc
// request
{"id": 1, "cmd": "vehicle_spawn", "modelId": 411}
// response
{"id": 1, "ok": true, "data": {"handle": 12}}
{"id": 1, "ok": false, "error": "..."}
```

Commands queue while not `in_game`, with a 15 s TTL. Failures surface as `ok: false`, never fabricated data.

---

## Environment variables

| Variable | Default | Purpose |
|---|---|---|
| `GTASA_PATH` | (registry) | Game directory |
| `GTASA_PIPE_TIMEOUT` | `5000` | Pipe connect timeout (ms) |
| `GTASA_LAUNCH_WAIT` | `15000` | Max wait for process to appear (ms) |
| `PLUGIN_SDK_ROOT` | `../plugin-sdk-sa` | plugin-sdk location, build time only |

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| `Pipe connect timeout` | Bridge not loaded, `mcp_debug.lock` missing, or game not started via `game_launch` |
| `queued command expired waiting for gameplay (15s)` | Scene isn't `in_game` — check `game_state` |
| `LoadLibrary failed with error 193` | Plugin built x64; rebuild `Platform=Win32` |
| `LoadLibrary failed with error 126` | Missing dependency DLL next to the plugin |
| `cannot reload debug_bridge.asi itself` | By design — restart the game |
| `GtaMcpDebugLog/GtaMcpDebugLen not exported` | Add both exports to your plugin |
| `game_launch` says already running | A stale `gta_sa.exe` is alive; `game_kill` first |

---

## License

MIT
