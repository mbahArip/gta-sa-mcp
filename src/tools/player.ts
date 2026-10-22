import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerPlayerTools(server: McpServer) {
  server.tool(
    "player_get",
    "Get CJ's current state: position, heading, health, armour, maxHealth, wanted level, money, interior, on-foot or in-vehicle.",
    {},
    async () => {
      const data = await send("player_get");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_set",
    "Set one or more CJ properties (health, maxHealth, armour, wanted, money, invincible). All fields optional.",
    {
      health:     z.number().min(0).max(1000).optional().describe("Health (0–1000, default max 200)"),
      maxHealth:  z.number().min(0).max(1000).optional().describe("Max health value"),
      armour:     z.number().min(0).max(1000).optional().describe("Armour (0–1000)"),
      wanted:     z.number().int().min(0).max(6).optional().describe("Wanted level (0–6)"),
      money:      z.number().int().optional().describe("Money amount"),
      invincible: z.boolean().optional().describe("Enable/disable damage invulnerability (god mode)"),
    },
    async ({ health, maxHealth, armour, wanted, money, invincible }) => {
      const data = await send("player_set", { health, maxHealth, armour, wanted, money, invincible });
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
    "player_enter_vehicle",
    "Make CJ walk to a vehicle and get in as driver (task-based). Use vehicle_spawn's returned handle.",
    {
      vehicleHandle: z.number().int().describe("Vehicle pool handle from vehicle_spawn"),
    },
    async ({ vehicleHandle }) => {
      const data = await send("player_enter_vehicle", { vehicleHandle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_give_weapon",
    "Give CJ a weapon with ammo.",
    {
      weaponType: z.number().int().describe("eWeaponType ID (e.g. 30 = AK47, 31 = M4, 22 = Colt45, 35 = Rocket Launcher)"),
      ammo:       z.number().int().min(1).describe("Ammo count"),
      setActive:  z.boolean().optional().describe("Switch to this weapon after giving (default true)"),
    },
    async ({ weaponType, ammo, setActive }) => {
      const data = await send("player_give_weapon", { weaponType, ammo, setActive: setActive ?? true });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_remove_weapon",
    "Remove a specific weapon from CJ's inventory.",
    {
      weaponType: z.number().int().describe("eWeaponType ID to remove"),
    },
    async ({ weaponType }) => {
      const data = await send("player_remove_weapon", { weaponType });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_clear_weapons",
    "Remove all weapons from CJ.",
    {},
    async () => {
      const data = await send("player_clear_weapons");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_set_skin",
    "Change CJ's player skin / ped model by ID (e.g. 0=CJ, 105=Fam1, 264=Scientist).",
    {
      modelId: z.number().int().min(0).max(312).describe("Ped model index (0–312)"),
    },
    async ({ modelId }) => {
      const data = await send("player_set_skin", { modelId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_set_stat",
    "Set CJ stat value (eStats ID e.g. 21=Fat, 23=Muscle, 225=Max Health, 24=Max Stamina, 160=Driving Skill).",
    {
      statId: z.number().int().min(0).max(350).describe("eStats enum index (see sdk_lookup for stat IDs)"),
      value:  z.number().describe("New stat value (usually 0.0 to 1000.0)"),
    },
    async ({ statId, value }) => {
      const data = await send("player_set_stat", { statId, value });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_get_stat",
    "Get CJ stat value by stat ID.",
    {
      statId: z.number().int().min(0).max(350).describe("eStats enum index"),
    },
    async ({ statId }) => {
      const data = await send("player_get_stat", { statId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "player_clear_wanted",
    "Instantly clear CJ's wanted level to 0 stars.",
    {},
    async () => {
      const data = await send("player_clear_wanted");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
