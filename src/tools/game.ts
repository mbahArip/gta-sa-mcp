import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import * as proc from "../process.ts";
import { isConnected, ensureConnected, disconnect } from "../pipe.ts";
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
    "Check if GTA SA is running and whether the debug bridge is connected.",
    {},
    async () => {
      const running = await proc.isGameRunning();
      let bridgeConnected = false;
      if (running && !isConnected()) {
        try { await ensureConnected(1000); bridgeConnected = true; } catch { /* not ready */ }
      } else {
        bridgeConnected = isConnected();
      }
      return {
        content: [{
          type: "text",
          text: JSON.stringify({ running, bridgeConnected }, null, 2),
        }],
      };
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
