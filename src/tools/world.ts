import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerWorldTools(server: McpServer) {
  server.tool(
    "world_set_time",
    "Set the in-game clock.",
    {
      hour:   z.number().int().min(0).max(23),
      minute: z.number().int().min(0).max(59).optional(),
    },
    async ({ hour, minute }) => {
      const data = await send("world_set_time", { hour, minute: minute ?? 0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_weather",
    "Set weather by ID (0=sunny, 1=cloudy, 2=rainy, 3=foggy, 4=extra sunny, 7=hurricane, 10=sandstorm).",
    {
      weatherId: z.number().int().min(0).max(45),
    },
    async ({ weatherId }) => {
      const data = await send("world_set_weather", { weatherId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_pool_dump",
    "Dump all peds and vehicles within a radius of CJ's position.",
    {
      radius: z.number().min(1).max(500).optional().describe("Search radius in units (default 50)"),
    },
    async ({ radius }) => {
      const data = await send("world_pool_dump", { radius: radius ?? 50.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_get_time",
    "Get current in-game time and date.",
    {},
    async () => {
      const data = await send("world_get_time");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_timescale",
    "Set game time scale for slow-motion or freeze (0 = frozen, 1 = normal, up to 10). Useful for observing transient plugin states.",
    {
      scale: z.number().min(0).max(10).describe("Time scale (0=frozen, 1=normal)"),
    },
    async ({ scale }) => {
      const data = await send("world_set_timescale", { scale });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_explosion",
    "Trigger an explosion at world coordinates — for testing damage/impact event handlers in plugins.",
    {
      x:    z.number().describe("World X"),
      y:    z.number().describe("World Y"),
      z:    z.number().describe("World Z"),
      type: z.number().int().min(0).max(25).optional().describe("eExplosionType (default 0 = grenade)"),
    },
    async ({ x, y, z, type }) => {
      const data = await send("world_explosion", { x, y, z, type: type ?? 0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "radar_get_blips",
    "List all active radar blips (waypoints, mission markers, entity blips) with position, colour, sprite, and attached entity handle.",
    {},
    async () => {
      const data = await send("radar_blips");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
