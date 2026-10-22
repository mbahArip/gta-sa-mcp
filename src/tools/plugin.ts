import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { send } from "../pipe.ts";

export function registerPluginTools(server: McpServer) {
  server.tool(
    "plugin_load",
    "Dynamically load an .asi or .dll plugin into the running GTA SA process.",
    {
      path: z.string().describe("Filename (in game folder e.g. 'myplugin.asi') or full absolute path to the .asi/.dll"),
    },
    async ({ path }) => {
      const data = await send("plugin_load", { path });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "plugin_unload",
    "Unload a previously loaded plugin module from the GTA SA process. Make sure the plugin cleaned its hooks/events before unloading.",
    {
      pluginName: z.string().describe("Module filename (e.g. 'myplugin.asi')"),
    },
    async ({ pluginName }) => {
      const data = await send("plugin_unload", { pluginName });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "plugin_reload",
    "Hot reload an ASI plugin without restarting GTA SA. Unloads the old module from memory and loads the fresh build.",
    {
      pluginName: z.string().optional().describe("Module name to unload (e.g. 'myplugin.asi')"),
      path:       z.string().optional().describe("Path to the fresh .asi/.dll build (defaults to pluginName)"),
    },
    async ({ pluginName, path }) => {
      const data = await send("plugin_reload", {
        pluginName: pluginName ?? "",
        path: path ?? pluginName ?? "",
      });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );

  server.tool(
    "plugin_log_read",
    "Read the debug log exported by a loaded .asi plugin. Plugin must export 'GtaMcpDebugLog' (char[]) and 'GtaMcpDebugLen' (int).",
    {
      pluginName: z.string().describe("ASI filename e.g. 'myplugin.asi'"),
      clear:      z.boolean().optional().describe("Clear the log buffer after reading (default false)"),
    },
    async ({ pluginName, clear }) => {
      const data = await send("plugin_log_read", { pluginName, clear: clear ?? false });
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    }
  );
}
