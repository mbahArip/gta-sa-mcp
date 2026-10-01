import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { loadConfig } from "./config.ts";
import { deleteSentinel } from "./process.ts";
import { disconnect } from "./pipe.ts";
import { registerGameTools } from "./tools/game.ts";
import { registerPlayerTools } from "./tools/player.ts";
import { registerVehicleTools } from "./tools/vehicle.ts";
import { registerPedTools } from "./tools/ped.ts";
import { registerWorldTools } from "./tools/world.ts";
import { registerMemoryTools } from "./tools/memory.ts";

const cfg = await loadConfig();

const server = new McpServer({
  name: "gta-sa-mcp",
  version: "1.0.0",
});

registerGameTools(server, cfg);
registerPlayerTools(server);
registerVehicleTools(server);
registerPedTools(server);
registerWorldTools(server);
registerMemoryTools(server);

// Clean up sentinel + pipe on exit
function cleanup() {
  disconnect();
  deleteSentinel(cfg);
}
process.on("exit", cleanup);
process.on("SIGINT", () => { cleanup(); process.exit(0); });
process.on("SIGTERM", () => { cleanup(); process.exit(0); });

const transport = new StdioServerTransport();
await server.connect(transport);
