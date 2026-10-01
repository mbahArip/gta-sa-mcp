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
      handle:        z.number().int().describe("Ped pool handle"),
      task:          z.enum(["wander", "follow_player", "attack_player", "flee_player", "hands_up", "die", "enter_vehicle", "play_anim"])
                       .describe("Task to assign"),
      vehicleHandle: z.number().int().optional().describe("Target vehicle (required for enter_vehicle)"),
      animName:      z.string().optional().describe("Animation name (required for play_anim, e.g. 'dance_loop')"),
      animLib:       z.string().optional().describe("Animation library (default 'ped', e.g. 'DANCING')"),
      animTime:      z.number().int().optional().describe("Anim duration in ms (default 5000)"),
    },
    async ({ handle, task, vehicleHandle, animName, animLib, animTime }) => {
      const data = await send("ped_set_task", { handle, task, vehicleHandle, animName, animLib, animTime });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_remove",
    "Remove a spawned NPC from the world (cleanup). Fails for the player ped.",
    {
      handle: z.number().int().describe("Ped pool handle"),
    },
    async ({ handle }) => {
      const data = await send("ped_remove", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_teleport",
    "Teleport a ped to world coordinates.",
    {
      handle: z.number().int().describe("Ped pool handle"),
      x: z.number().describe("World X"),
      y: z.number().describe("World Y"),
      z: z.number().describe("World Z"),
    },
    async ({ handle, x, y, z }) => {
      const data = await send("ped_teleport", { handle, x, y, z });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_give_weapon",
    "Give a weapon to a spawned NPC (or any ped by handle) and optionally make it the active weapon.",
    {
      handle:     z.number().int().describe("Ped pool handle from ped_spawn"),
      weaponType: z.number().int().describe("eWeaponType ID (e.g. 22=pistol, 30=AK47, 31=M4)"),
      ammo:       z.number().int().min(1).optional().describe("Ammo count (default 100)"),
      setActive:  z.boolean().optional().describe("Switch ped to this weapon (default true)"),
    },
    async ({ handle, weaponType, ammo, setActive }) => {
      const data = await send("ped_give_weapon", {
        handle,
        weaponType,
        ammo: ammo ?? 100,
        setActive: setActive ?? true,
      });
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
