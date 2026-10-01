import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerVehicleTools(server: McpServer) {
  server.tool(
    "vehicle_spawn",
    "Spawn a vehicle near CJ by model ID. Returns a handle for subsequent vehicle_get/set calls.",
    {
      modelId: z.number().int().min(400).max(611).describe("Vehicle model ID (400–611, e.g. 411=Infernus, 521=FCR-900)"),
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
    "Get state of the vehicle CJ is currently in (or by handle). Returns model, speed, health, gear, door lock, engine state.",
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
    "Set properties of a vehicle. All fields optional.",
    {
      handle:   z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
      health:   z.number().min(0).max(1000).optional().describe("Vehicle health (0–1000)"),
      doorLock: z.number().int().min(0).max(8).optional().describe("Door lock state (0=unlocked, 2=locked, etc.)"),
      engine:   z.boolean().optional().describe("Engine on/off"),
    },
    async ({ handle, health, doorLock, engine }) => {
      const data = await send("vehicle_set", { handle, health, doorLock, engine });
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
    "Repair a vehicle (full health, un-flipped).",
    {
      handle: z.number().int().optional().describe("Vehicle pool handle (omit to use CJ's current vehicle)"),
    },
    async ({ handle }) => {
      const data = await send("vehicle_fix", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
