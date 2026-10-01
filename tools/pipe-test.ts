// Direct pipe test client — mirrors src/pipe.ts protocol without the MCP layer.
// Usage: bun tools/pipe-test.ts '<json-command>'
//   bun tools/pipe-test.ts '{"cmd":"game_state"}'

const net = require("node:net");

const PIPE_NAME = "\\\\.\\pipe\\gtasa_mcp_bridge";
const arg = process.argv[2];
if (!arg) {
  console.error("usage: bun tools/pipe-test.ts '{\"cmd\":\"...\",...}'");
  process.exit(1);
}

let parsed: Record<string, unknown>;
try {
  parsed = JSON.parse(arg);
} catch (e) {
  console.error("invalid JSON:", String(e));
  process.exit(1);
}

const msg = JSON.stringify({ id: 1, ...parsed }) + "\n";
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
}, 4000);
