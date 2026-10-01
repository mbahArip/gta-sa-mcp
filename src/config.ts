/**
 * Config resolution: --gamePath CLI arg → GTASA_PATH env → Windows registry → error
 */

const REGISTRY_KEY = "HKLM\\SOFTWARE\\WOW6432Node\\Rockstar Games\\GTA San Andreas";
const REGISTRY_VALUE = "InstallFolder";

function parseCliArgs(): Record<string, string> {
  const result: Record<string, string> = {};
  for (const arg of process.argv.slice(2)) {
    const match = arg.match(/^--([a-zA-Z]+)=(.+)$/);
    if (match?.[1] && match[2]) result[match[1]] = match[2];
  }
  return result;
}

async function readRegistry(): Promise<string | null> {
  try {
    const proc = Bun.spawn(["reg", "query", REGISTRY_KEY, "/v", REGISTRY_VALUE], {
      stdout: "pipe",
      stderr: "pipe",
    });
    await proc.exited;
    if (proc.exitCode !== 0) return null;
    const out = await new Response(proc.stdout).text();
    const match = out.match(/InstallFolder\s+REG_SZ\s+(.+)/);
    return match?.[1] ? match[1].trim() : null;
  } catch {
    return null;
  }
}

export type Config = {
  gamePath: string;
  pipeTimeout: number;
  launchWaitMs: number;
};

export async function loadConfig(): Promise<Config> {
  const args = parseCliArgs();

  const gamePath =
    args["gamePath"] ??
    process.env["GTASA_PATH"] ??
    (await readRegistry()) ??
    null;

  if (!gamePath) {
    throw new Error(
      "GTA SA path not found. Provide --gamePath=<path>, set GTASA_PATH env, or install GTA SA to the default location."
    );
  }

  return {
    gamePath: gamePath.replace(/\\/g, "/").replace(/\/$/, ""),
    pipeTimeout: parseInt(process.env["GTASA_PIPE_TIMEOUT"] ?? "5000"),
    launchWaitMs: parseInt(process.env["GTASA_LAUNCH_WAIT"] ?? "15000"),
  };
}
