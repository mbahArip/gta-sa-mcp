import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerCameraTools(server: McpServer) {
  server.tool(
    "camera_get",
    "Get current camera position, front direction vector, FOV, and active camera mode.",
    {},
    async () => {
      const data = await send("camera_get");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_set_pos",
    "Place the camera at a fixed world position, optionally aiming at target coordinates.",
    {
      x:       z.number().describe("Camera X position"),
      y:       z.number().describe("Camera Y position"),
      z:       z.number().describe("Camera Z position"),
      targetX: z.number().optional().describe("Look-at target X"),
      targetY: z.number().optional().describe("Look-at target Y"),
      targetZ: z.number().optional().describe("Look-at target Z"),
    },
    async ({ x, y, z: cz, targetX, targetY, targetZ }) => {
      const data = await send("camera_set_pos", { x, y, z: cz, targetX, targetY, targetZ });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_point_at",
    "Point the camera at an entity (ped or vehicle handle) or fixed coordinates.",
    {
      pedHandle:     z.number().int().optional().describe("Ped handle to point at"),
      vehicleHandle: z.number().int().optional().describe("Vehicle handle to point at"),
      x:             z.number().optional().describe("Target X"),
      y:             z.number().optional().describe("Target Y"),
      z:             z.number().optional().describe("Target Z"),
    },
    async ({ pedHandle, vehicleHandle, x, y, z: tz }) => {
      const data = await send("camera_point_at", { pedHandle, vehicleHandle, x, y, z: tz });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_restore",
    "Restore standard 3rd person follow camera behind player.",
    {},
    async () => {
      const data = await send("camera_restore");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_fade",
    "Fade screen in or out over duration (seconds).",
    {
      duration:  z.number().min(0.1).max(10).describe("Fade duration in seconds"),
      direction: z.enum(["in", "out"]).describe("Fade 'in' (to game) or 'out' (to black)"),
    },
    async ({ duration, direction }) => {
      const data = await send("camera_fade", { duration, direction });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_shake",
    "Apply camera shake effect.",
    {
      intensity: z.number().min(0.1).max(10).optional().describe("Shake intensity (default 1.0)"),
    },
    async ({ intensity }) => {
      const data = await send("camera_shake", { intensity: intensity ?? 1.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "camera_set_mode",
    "Set camera mode (eCamMode ID e.g. 4=FOLLOW_PED, 18=AIMING, 1=CINEMATIC, 3=FIXED).",
    {
      mode: z.number().int().min(0).max(60).describe("eCamMode index"),
    },
    async ({ mode }) => {
      const data = await send("camera_set_mode", { mode });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
