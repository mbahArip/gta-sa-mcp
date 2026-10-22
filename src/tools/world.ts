import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerWorldTools(server: McpServer) {
  server.tool(
    "world_set_time",
    "Set the in-game clock (hour and minute).",
    {
      hour:   z.number().int().min(0).max(23).describe("Hour (0–23)"),
      minute: z.number().int().min(0).max(59).optional().describe("Minute (0–59)"),
    },
    async ({ hour, minute }) => {
      const data = await send("world_set_time", { hour, minute: minute ?? 0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_get_time",
    "Get current in-game clock time.",
    {},
    async () => {
      const data = await send("world_get_time");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_weather",
    "Set weather by ID (0=sunny, 1=cloudy, 2=rainy, 3=foggy, 4=extra sunny, 7=hurricane, 8=underwater, 9=extra colours, 10=sandstorm, 11=foggy cloudy).",
    {
      weatherId: z.number().int().min(0).max(45).describe("Weather ID (0–45)"),
    },
    async ({ weatherId }) => {
      const data = await send("world_set_weather", { weatherId });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_timescale",
    "Set game time scale for slow-motion or freeze (0 = frozen, 1 = normal, up to 10).",
    {
      scale: z.number().min(0).max(10).describe("Time scale multiplier (0=frozen, 1=normal, >1=fast)"),
    },
    async ({ scale }) => {
      const data = await send("world_set_timescale", { scale });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_gravity",
    "Set global world physics gravity (default 0.008f).",
    {
      gravity: z.number().describe("Gravity value (default 0.008, 0.0=zero gravity)"),
    },
    async ({ gravity }) => {
      const data = await send("world_set_gravity", { gravity });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_traffic_density",
    "Set vehicle traffic spawn density multiplier.",
    {
      multiplier: z.number().min(0).max(10).describe("Density multiplier (0.0=no traffic, 1.0=normal, up to 10.0)"),
    },
    async ({ multiplier }) => {
      const data = await send("world_set_traffic_density", { multiplier });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_set_ped_density",
    "Set pedestrian spawn density multiplier.",
    {
      multiplier: z.number().min(0).max(10).describe("Density multiplier (0.0=no peds, 1.0=normal, up to 10.0)"),
    },
    async ({ multiplier }) => {
      const data = await send("world_set_ped_density", { multiplier });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_explosion",
    "Trigger an explosion at world coordinates.",
    {
      x:    z.number().describe("World X"),
      y:    z.number().describe("World Y"),
      z:    z.number().describe("World Z"),
      type: z.number().int().min(0).max(25).optional().describe("eExplosionType (default 0=grenade, 1=molotov, 2=rocket, 3=car, 4=car quick, 5=boat, 6=heli)"),
    },
    async ({ x, y, z, type }) => {
      const data = await send("world_explosion", { x, y, z, type: type ?? 0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_create_object",
    "Spawn an in-game prop/object by model ID at world coordinates (e.g. 1225=explosive barrel, 1425=wooden crate, 1238=traffic cone, ramps).",
    {
      modelId: z.number().int().describe("Object model ID"),
      x:       z.number().describe("World X"),
      y:       z.number().describe("World Y"),
      z:       z.number().describe("World Z"),
    },
    async ({ modelId, x, y, z }) => {
      const data = await send("world_create_object", { modelId, x, y, z });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_remove_object",
    "Remove a spawned object by handle.",
    {
      handle: z.number().int().describe("Object handle returned from world_create_object"),
    },
    async ({ handle }) => {
      const data = await send("world_remove_object", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_create_pickup",
    "Spawn a collectible pickup (weapons, health, armor, money, bribes) at world coordinates.",
    {
      x:          z.number().describe("World X"),
      y:          z.number().describe("World Y"),
      z:          z.number().describe("World Z"),
      modelId:    z.number().int().optional().describe("Pickup model ID (e.g. 1240=heart/health, 1242=armor, 1212=money)"),
      weaponType: z.number().int().optional().describe("eWeaponType ID (if creating a weapon pickup directly)"),
      ammo:       z.number().int().optional().describe("Ammo for weapon pickup (default 100)"),
      pickupType: z.number().int().optional().describe("ePickupType (default 3 = once/on street)"),
    },
    async ({ x, y, z, modelId, weaponType, ammo, pickupType }) => {
      const data = await send("world_create_pickup", {
        x,
        y,
        z,
        modelId: modelId ?? 0,
        weaponType: weaponType ?? 0,
        ammo: ammo ?? 100,
        pickupType: pickupType ?? 3,
      });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_remove_pickup",
    "Remove a pickup by its unique pickup handle.",
    {
      handle: z.number().int().describe("Pickup handle from world_create_pickup"),
    },
    async ({ handle }) => {
      const data = await send("world_remove_pickup", { handle });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_create_fire",
    "Spawn a fire at world coordinates.",
    {
      x:    z.number().describe("World X"),
      y:    z.number().describe("World Y"),
      z:    z.number().describe("World Z"),
      size: z.number().optional().describe("Fire particle size (default 1.0)"),
    },
    async ({ x, y, z, size }) => {
      const data = await send("world_create_fire", { x, y, z, size: size ?? 1.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_extinguish_fires",
    "Extinguish all fires around coordinates within a radius.",
    {
      x:      z.number().describe("World X"),
      y:      z.number().describe("World Y"),
      z:      z.number().describe("World Z"),
      radius: z.number().optional().describe("Extinguish radius in meters (default 25.0)"),
    },
    async ({ x, y, z, radius }) => {
      const data = await send("world_extinguish_fires", { x, y, z, radius: radius ?? 25.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "world_pool_dump",
    "Dump all active peds and vehicles within a radius of CJ's position.",
    {
      radius: z.number().min(1).max(500).optional().describe("Search radius in units (default 50)"),
    },
    async ({ radius }) => {
      const data = await send("world_pool_dump", { radius: radius ?? 50.0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "radar_get_blips",
    "List all active radar blips (waypoints, mission markers, entity blips) with position, color, sprite, and attached handle.",
    {},
    async () => {
      const data = await send("radar_blips");
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "radar_add_blip",
    "Add a custom blip / marker to the radar and map.",
    {
      x:        z.number().describe("World X"),
      y:        z.number().describe("World Y"),
      z:        z.number().describe("World Z"),
      color:    z.number().int().optional().describe("Blip color index (0=red, 1=green, 2=blue, 3=white, 4=yellow)"),
      spriteId: z.number().int().optional().describe("Radar sprite icon ID (0=square/waypoint, 1=destination, 23=skull, etc.)"),
    },
    async ({ x, y, z, color, spriteId }) => {
      const data = await send("radar_add_blip", { x, y, z, color: color ?? 2, sprite: spriteId ?? 0 });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "radar_remove_blip",
    "Remove a custom blip by blip index.",
    {
      blipIndex: z.number().int().describe("Blip index returned from radar_add_blip"),
    },
    async ({ blipIndex }) => {
      const data = await send("radar_remove_blip", { blipIndex });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
