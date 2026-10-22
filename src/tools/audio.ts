import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerAudioTools(server: McpServer) {
  server.tool(
    "audio_play_sound",
    "Play a 2D frontend sound effect by sound ID.",
    {
      soundId: z.number().int().describe("Sound FX ID (e.g. 1052=pickup, 1056=door open, 1057=door close, 1083=mission passed)"),
      volume:  z.number().min(0).max(1).optional().describe("Volume level (0.0 to 1.0, default 1.0)"),
    },
    async ({ soundId, volume }) => {
      const data = await send("audio_play_sound", { soundId, volume: volume ?? 1.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "audio_set_radio",
    "Switch radio station by station ID (0=Playback FM, 1=K Rose, 2=K-DST, 3=Bounce FM, 4=SF-UR, 5=Radio Los Santos, 6=Radio X, 7=CSR, 8=K-JAH, 9=Master Sounds, 10=WCTR, 12=Radio Off).",
    {
      stationId: z.number().int().min(0).max(12).describe("Radio station ID (0–12, 12=OFF)"),
    },
    async ({ stationId }) => {
      const data = await send("audio_set_radio", { stationId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
