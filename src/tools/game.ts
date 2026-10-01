import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import * as proc from "../process.ts";
import { isConnected, ensureConnected, disconnect, send, getCachedGameState } from "../pipe.ts";
import type { Config } from "../config.ts";

export function registerGameTools(server: McpServer, cfg: Config) {
  server.tool(
    "game_launch",
    "Launch GTA SA with debug bridge active. Creates mcp_debug.lock, spawns gta_sa.exe, waits for pipe connection.",
    {},
    async () => {
      if (await proc.isGameRunning()) {
        return { content: [{ type: "text", text: "GTA SA is already running." }] };
      }
      await proc.launchGame(cfg, cfg.launchWaitMs);
      try {
        await ensureConnected(cfg.pipeTimeout);
        return { content: [{ type: "text", text: "GTA SA launched and bridge connected." }] };
      } catch {
        return { content: [{ type: "text", text: "GTA SA launched but bridge not yet connected — game may still be loading. Try game_status in a moment." }] };
      }
    }
  );

  server.tool(
    "game_status",
    "Check if GTA SA is running, whether the debug bridge is connected, and the current scene (from the 100ms state cache).",
    {},
    async () => {
      const running = await proc.isGameRunning();
      let bridgeConnected = false;
      if (running && !isConnected()) {
        try { await ensureConnected(1000); bridgeConnected = true; } catch { /* not ready */ }
      } else {
        bridgeConnected = isConnected();
      }
      const state = bridgeConnected ? getCachedGameState() : null;
      return {
        content: [{
          type: "text",
          text: JSON.stringify({ running, bridgeConnected, state }, null, 2),
        }],
      };
    }
  );

  server.tool(
    "game_state",
    "Detect the game's current scene: loading (splash/intro), menu, paused, cutscene, or in_game. Served from the 100ms state cache when fresh — use it to know whether gameplay commands can run.",
    {},
    async () => {
      const cached = getCachedGameState();
      if (cached) {
        return { content: [{ type: "text", text: JSON.stringify({ ...cached, cached: true }, null, 2) }] };
      }
      if (!isConnected()) {
        try { await ensureConnected(2000); } catch {
          return { content: [{ type: "text", text: "Bridge not connected." }] };
        }
      }
      try {
        const state = await send("game_state", {}, 2000);
        return { content: [{ type: "text", text: JSON.stringify(state, null, 2) }] };
      } catch (e) {
        return { content: [{ type: "text", text: `Bridge not responding: ${e instanceof Error ? e.message : String(e)}` }] };
      }
    }
  );

  server.tool(
    "key_tap",
    "Simulate a keyboard key press inside the game for N frames — tests plugin hotkey handlers without touching the real keyboard.",
    {
      keyCode: z.number().int().min(1).max(255).describe("Windows VK_ code (e.g. 0x70=VK_F1, 0x31='1', 0x09=VK_TAB)"),
      frames:  z.number().int().min(1).max(60).optional().describe("Frames to hold the key down (default 2)"),
    },
    async ({ keyCode, frames }) => {
      const data = await send("key_tap", { keyCode, frames: frames ?? 2 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "screenshot",
    "Capture the GTA SA window to a PNG and return the file path — lets you visually inspect plugin effects.",
    {},
    async () => {
      const ts = Date.now();
      const outPath = `${process.env["TEMP"] ?? "C:\\Temp"}\\gta-sa-mcp-shot-${ts}.png`;
      const script = `
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$h = [W]::FindWindow($null, "GTA: San Andreas")
if ($h -eq [IntPtr]::Zero) { Write-Output "ERROR window not found"; exit 1 }
$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { Write-Output "ERROR invalid window size"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save("${outPath}", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "OK"
`;
      const proc = Bun.spawn(["powershell", "-NoProfile", "-Command", script], { stdout: "pipe", stderr: "pipe" });
      await proc.exited;
      const out = (await new Response(proc.stdout).text()).trim();
      if (out !== "OK") {
        return { content: [{ type: "text", text: `Screenshot failed: ${out}` }] };
      }
      return { content: [{ type: "text", text: outPath }] };
    }
  );

  server.tool(
    "game_kill",
    "Kill GTA SA process and remove the sentinel file.",
    {},
    async () => {
      disconnect();
      await proc.killGame(cfg);
      return { content: [{ type: "text", text: "GTA SA killed and sentinel removed." }] };
    }
  );
}
