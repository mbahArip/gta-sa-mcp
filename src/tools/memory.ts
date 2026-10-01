import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerMemoryTools(server: McpServer) {
  server.tool(
    "memory_read",
    "Read raw bytes from a GTA SA memory address. Escape hatch for inspecting plugin state directly.",
    {
      address: z.number().int().describe("SA 1.0 US address (e.g. 0xB7CD98)"),
      size:    z.number().int().min(1).max(1024).describe("Number of bytes to read (max 1024)"),
      type:    z.enum(["hex", "int32", "float", "string"]).optional().describe("How to interpret (default hex)"),
    },
    async ({ address, size, type }) => {
      const data = await send("memory_read", { address, size, type: type ?? "hex" });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "memory_write",
    "Write raw bytes to a GTA SA memory address. Use with care.",
    {
      address: z.number().int().describe("SA 1.0 US address"),
      type:    z.enum(["int32", "float", "bytes"]).describe("Value type"),
      value:   z.union([z.number(), z.string()]).describe("Value to write (bytes as hex string e.g. '90 90 90')"),
    },
    async ({ address, type, value }) => {
      const data = await send("memory_write", { address, type, value });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "plugin_log_read",
    "Read the debug log exported by a loaded .asi plugin. Plugin must export 'GtaMcpDebugLog' (char[]) and 'GtaMcpDebugLen' (int).",
    {
      pluginName: z.string().describe("ASI filename e.g. 'myplugin.asi'"),
      clear:      z.boolean().optional().describe("Clear the log after reading (default false)"),
    },
    async ({ pluginName, clear }) => {
      const data = await send("plugin_log_read", { pluginName, clear: clear ?? false });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
