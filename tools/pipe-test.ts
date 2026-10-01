// Direct pipe test client — mirrors src/pipe.ts protocol without the MCP layer.
// Usage: bun tools/pipe-test.ts <cmd> [key=value ...]
//   bun tools/pipe-test.ts game_state
//   bun tools/pipe-test.ts vehicle_spawn modelId=477 offsetX=0

const net = require("node:net");

const PIPE_NAME = "\\\\.\\pipe\\gtasa_mcp_bridge";
const [cmd, ...pairs] = process.argv.slice(2);
if (!cmd) {
  console.error("usage: bun tools/pipe-test.ts <cmd> [key=value ...]");
  process.exit(1);
}

const params: Record<string, number | boolean | string> = {};
for (const pair of pairs) {
  const eq = pair.indexOf("=");
  if (eq === -1) { console.error(`bad pair: ${pair}`); process.exit(1); }
  const key = pair.slice(0, eq);
  const raw = pair.slice(eq + 1);
  params[key] = raw === "true" ? true
    : raw === "false" ? false
    : raw !== "" && !Number.isNaN(Number(raw)) ? Number(raw)
    : raw;
}

const msg = JSON.stringify({ id: 1, cmd, ...params }) + "\n";
const sock = net.connect(PIPE_NAME);

sock.on("connect", () => sock.write(msg));

let buf = "";
sock.on("data", (d: Buffer) => {
  buf += d.toString("utf8");
  const idx = buf.indexOf("\n");
  if (idx !== -1) {
    console.log(buf.slice(0, idx).trim());
    sock.destroy();
    process.exit(0);
  }
});

sock.on("error", (e: Error) => {
  console.error("ERR", e.message);
  process.exit(1);
});

setTimeout(() => {
  console.error("timeout waiting for response");
  process.exit(1);
}, 45000);
