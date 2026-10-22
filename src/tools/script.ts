import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerScriptTools(server: McpServer) {
  server.tool(
    "script_call_opcode",
    "Dynamically execute any GTA SA script opcode (SCM command). Unlocks all ~2000 GTA SA script commands directly.",
    {
      opcode: z.number().int().describe("Opcode number (e.g. 0x0107 for SET_WEATHER, 0x02AB for SET_RADIO_CHANNEL)"),
      types:  z.string().optional().describe("Comma-separated argument types: 'int', 'float', 'str', 'ped', 'veh', 'obj' (e.g. 'int,float')"),
      values: z.string().optional().describe("Comma-separated argument values matching types (e.g. '411,10.5')"),
    },
    async ({ opcode, types, values }) => {
      const data = await send("script_call_opcode", {
        opcode,
        types: types ?? "",
        values: values ?? "",
      });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
