import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerPlayerTools(server: McpServer) {
  server.tool(
    "player_get",
    "Get CJ's current state: position, heading, health, armour, wanted level, money, interior, on-foot or in-vehicle.",
    {},
    async () => {
      const data = await send("player_get");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_set",
    "Set one or more CJ properties. All fields optional.",
    {
      health:  z.number().min(0).max(200).optional().describe("Health (0–200)"),
      armour:  z.number().min(0).max(200).optional().describe("Armour (0–200)"),
      wanted:  z.number().int().min(0).max(6).optional().describe("Wanted level (0–6)"),
      money:   z.number().int().optional().describe("Money amount"),
    },
    async ({ health, armour, wanted, money }) => {
      const data = await send("player_set", { health, armour, wanted, money });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_teleport",
    "Teleport CJ to world coordinates.",
    {
      x:       z.number().describe("World X"),
      y:       z.number().describe("World Y"),
      z:       z.number().describe("World Z"),
      heading: z.number().optional().describe("Heading in degrees (0–360), optional"),
    },
    async ({ x, y, z: wz, heading }) => {
      const data = await send("player_teleport", { x, y, z: wz, heading });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_give_weapon",
    "Give CJ a weapon with ammo.",
    {
      weaponType: z.number().int().describe("eWeaponType ID (e.g. 31 = M4, 22 = Colt 45)"),
      ammo:       z.number().int().min(1).describe("Ammo count"),
      setActive:  z.boolean().optional().describe("Switch to this weapon after giving (default true)"),
    },
    async ({ weaponType, ammo, setActive }) => {
      const data = await send("player_give_weapon", { weaponType, ammo, setActive: setActive ?? true });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
