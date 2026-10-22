import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerPedTools(server: McpServer) {
  server.tool(
    "ped_spawn",
    "Spawn an NPC near CJ by model ID. Returns a handle.",
    {
      modelId: z.number().int().describe("Ped model ID (e.g. 105=Grove St gang, 264=scientist, 0=CJ)"),
      offsetX: z.number().optional().describe("X offset from CJ (default 3.0)"),
      offsetY: z.number().optional().describe("Y offset from CJ (default 0.0)"),
      pedType: z.number().int().optional().describe("ePedType (default 4 = PED_TYPE_CIVMALE, 1=player1, 6=cop, 7=gang1)"),
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
    "Get state of a ped by handle: position, health, armour, model, alive status.",
    {
      handle: z.number().int().describe("Ped pool handle from ped_spawn"),
    },
    async ({ handle }) => {
      const data = await send("ped_get", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_set",
    "Set ped properties directly (health, armour).",
    {
      handle: z.number().int().describe("Ped pool handle"),
      health: z.number().min(0).max(1000).optional().describe("Ped health"),
      armour: z.number().min(0).max(1000).optional().describe("Ped armour"),
    },
    async ({ handle, health, armour }) => {
      const data = await send("ped_set", { handle, health, armour });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_set_task",
    "Assign an AI task to a spawned NPC.",
    {
      handle:        z.number().int().describe("Ped pool handle"),
      task:          z.enum([
                       "wander",
                       "follow_player",
                       "attack_player",
                       "flee_player",
                       "hands_up",
                       "duck",
                       "jetpack",
                       "enter_vehicle",
                       "leave_vehicle",
                       "drive_to",
                       "play_anim",
                       "die",
                     ]).describe("Task action to assign"),
      vehicleHandle: z.number().int().optional().describe("Target vehicle (required for enter_vehicle)"),
      x:             z.number().optional().describe("Target X (for drive_to)"),
      y:             z.number().optional().describe("Target Y (for drive_to)"),
      z:             z.number().optional().describe("Target Z (for drive_to)"),
      speed:         z.number().optional().describe("Speed (for drive_to, default 20.0)"),
      animName:      z.string().optional().describe("Animation name (required for play_anim, e.g. 'dance_loop')"),
      animLib:       z.string().optional().describe("Animation library (default 'ped', e.g. 'DANCING')"),
      animTime:      z.number().int().optional().describe("Anim duration in ms (default 5000)"),
    },
    async ({ handle, task, vehicleHandle, x, y, z: tz, speed, animName, animLib, animTime }) => {
      const data = await send("ped_set_task", {
        handle,
        task,
        vehicleHandle,
        x,
        y,
        z: tz,
        speed,
        animName,
        animLib,
        animTime,
      });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_clear_tasks",
    "Clear all active and queued tasks from a ped, making them stop immediately.",
    {
      handle: z.number().int().describe("Ped pool handle"),
    },
    async ({ handle }) => {
      const data = await send("ped_clear_tasks", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_give_weapon",
    "Give a weapon to a spawned NPC (or any ped by handle) and optionally make it the active weapon.",
    {
      handle:     z.number().int().describe("Ped pool handle from ped_spawn"),
      weaponType: z.number().int().describe("eWeaponType ID (e.g. 22=pistol, 30=AK47, 31=M4, 29=MP5)"),
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
    "ped_teleport",
    "Teleport a ped to world coordinates.",
    {
      handle: z.number().int().describe("Ped pool handle"),
      x:      z.number().describe("World X"),
      y:      z.number().describe("World Y"),
      z:      z.number().describe("World Z"),
    },
    async ({ handle, x, y, z }) => {
      const data = await send("ped_teleport", { handle, x, y, z });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "ped_resurrect",
    "Revive / reset a dead or injured ped to full health and idle state.",
    {
      handle: z.number().int().describe("Ped pool handle"),
    },
    async ({ handle }) => {
      const data = await send("ped_resurrect", { handle });
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
}
