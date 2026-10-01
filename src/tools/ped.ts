import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerPedTools(server: McpServer) {
  server.tool(
    "ped_spawn",
    "Spawn an NPC near CJ by model ID. Returns a handle.",
    {
      modelId:  z.number().int().describe("Ped model ID (e.g. 264=scientist, 0=CJ model)"),
      offsetX:  z.number().optional().describe("X offset from CJ (default 3.0)"),
      offsetY:  z.number().optional().describe("Y offset from CJ (default 0.0)"),
      pedType:  z.number().int().optional().describe("ePedType (default 4 = PED_TYPE_CIVMALE)"),
    },
    async ({ modelId, offsetX, offsetY, pedType }) => {
      const data = await send("ped_spawn", {
        modelId,
        offsetX: offsetX ?? 3.0,
        offsetY: offsetY ?? 0.0,
        pedType: pedType ?? 4,
      });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_get",
    "Get state of a ped by handle: position, health, armour, model, current task type.",
    {
      handle: z.number().int().describe("Ped pool handle from ped_spawn"),
    },
    async ({ handle }) => {
      const data = await send("ped_get", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_set_task",
    "Assign a task to a spawned NPC.",
    {
      handle: z.number().int().describe("Ped pool handle"),
      task:   z.enum(["wander", "follow_player", "attack_player", "flee_player", "hands_up", "die"])
               .describe("Task to assign"),
    },
    async ({ handle, task }) => {
      const data = await send("ped_set_task", { handle, task });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_set",
    "Set ped properties directly.",
    {
      handle:  z.number().int().describe("Ped pool handle"),
      health:  z.number().min(0).max(200).optional(),
      armour:  z.number().min(0).max(200).optional(),
    },
    async ({ handle, health, armour }) => {
      const data = await send("ped_set", { handle, health, armour });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
