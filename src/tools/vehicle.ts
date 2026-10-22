import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerVehicleTools(server: McpServer) {
  server.tool(
    "vehicle_spawn",
    "Spawn a vehicle near CJ by model ID. Returns a handle for subsequent vehicle_get/set calls.",
    {
      modelId: z.number().int().min(400).max(611).describe("Vehicle model ID (400–611, e.g. 411=Infernus, 521=FCR-900, 425=Hunter, 520=Hydra)"),
      offsetX: z.number().optional().describe("X offset from CJ (default 5.0)"),
      offsetY: z.number().optional().describe("Y offset from CJ (default 0.0)"),
    },
    async ({ modelId, offsetX, offsetY }) => {
      const data = await send("vehicle_spawn", { modelId, offsetX: offsetX ?? 5.0, offsetY: offsetY ?? 0.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_get",
    "Get state of the vehicle CJ is currently in (or by handle). Returns model, speed, health, gear, door lock, primary/secondary colors, engine state.",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
    },
    async ({ handle }) => {
      const data = await send("vehicle_get", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_set",
    "Set properties of a vehicle (health, doorLock, engine, dirtLevel, alarm). All fields optional.",
    {
      handle:    z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      health:    z.number().min(0).max(1000).optional().describe("Vehicle health (0–1000)"),
      doorLock:  z.number().int().min(0).max(8).optional().describe("Door lock state (0=unlocked, 2=locked, etc.)"),
      engine:    z.boolean().optional().describe("Engine on/off"),
      dirtLevel: z.number().min(0).max(15).optional().describe("Dirt level (0.0=clean to 15.0=dirty)"),
      alarm:     z.number().int().optional().describe("Car alarm state"),
    },
    async ({ handle, health, doorLock, engine, dirtLevel, alarm }) => {
      const data = await send("vehicle_set", { handle, health, doorLock, engine, dirtLevel, alarm });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_set_color",
    "Set vehicle primary and secondary body colors (color palette IDs 0–127).",
    {
      handle:         z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      primaryColor:   z.number().int().min(0).max(255).describe("Primary color index (0–127)"),
      secondaryColor: z.number().int().min(0).max(255).optional().describe("Secondary color index (0–127, defaults to primary)"),
    },
    async ({ handle, primaryColor, secondaryColor }) => {
      const data = await send("vehicle_set_color", { handle, primaryColor, secondaryColor: secondaryColor ?? primaryColor });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_tune",
    "Install a tuning upgrade/mod component on a vehicle by model ID (e.g. 1010=10x Nitro, 1087=Hydraulics, 1025=Offroad Wheels, spoilers, exhausts).",
    {
      handle:         z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      upgradeModelId: z.number().int().min(1000).max(1193).describe("Tuning part model ID (1000–1193)"),
    },
    async ({ handle, upgradeModelId }) => {
      const data = await send("vehicle_tune", { handle, upgradeModelId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_pop_tire",
    "Burst a tire on a vehicle.",
    {
      handle:    z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      tireIndex: z.number().int().min(0).max(3).describe("Tire index (0=Front-Left, 1=Rear-Left, 2=Front-Right, 3=Rear-Right)"),
    },
    async ({ handle, tireIndex }) => {
      const data = await send("vehicle_pop_tire", { handle, tireIndex });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_set_door_state",
    "Open or close a specific door on a vehicle (0=hood, 1=boot/trunk, 2=front-left, 3=front-right, 4=rear-left, 5=rear-right).",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      doorId: z.number().int().min(0).max(5).describe("Door index (0=hood, 1=trunk, 2=FL, 3=FR, 4=RL, 5=RR)"),
      ratio:  z.number().min(0).max(1).optional().describe("Open ratio (0.0=closed, 1.0=fully open, default 1.0)"),
    },
    async ({ handle, doorId, ratio }) => {
      const data = await send("vehicle_set_door_state", { handle, doorId, ratio: ratio ?? 1.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_remove",
    "Remove a vehicle from the world (cleanup). Fails if CJ is inside it.",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
    },
    async ({ handle }) => {
      const data = await send("vehicle_remove", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_teleport",
    "Teleport a vehicle to world coordinates.",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      x: z.number().describe("World X"),
      y: z.number().describe("World Y"),
      z: z.number().describe("World Z"),
    },
    async ({ handle, x, y, z }) => {
      const data = await send("vehicle_teleport", { handle, x, y, z });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "vehicle_fix",
    "Repair a vehicle (full health, fix visual damage, un-flip).",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
    },
    async ({ handle }) => {
      const data = await send("vehicle_fix", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
