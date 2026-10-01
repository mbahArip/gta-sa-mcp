/**
 * GTA SA process management — launch, detect, kill, sentinel file.
 */

import { existsSync, writeFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import type { Config } from "./config.ts";

const SENTINEL_FILE = "mcp_debug.lock";
const EXE_NAME = "gta_sa.exe";

export function sentinelPath(cfg: Config): string {
  return join(cfg.gamePath, SENTINEL_FILE);
}

export function exePath(cfg: Config): string {
  return join(cfg.gamePath, EXE_NAME);
}

export async function findGamePid(): Promise<number | null> {
  try {
    const proc = Bun.spawn(
      ["powershell", "-NoProfile", "-Command",
       `(Get-Process -Name gta_sa -ErrorAction SilentlyContinue | Select-Object -First 1).Id`],
      { stdout: "pipe", stderr: "pipe" }
    );
    await proc.exited;
    const out = (await new Response(proc.stdout).text()).trim();
    const pid = parseInt(out);
    return isNaN(pid) ? null : pid;
  } catch {
    return null;
  }
}

export async function isGameRunning(): Promise<boolean> {
  return (await findGamePid()) !== null;
}

export function createSentinel(cfg: Config) {
  writeFileSync(sentinelPath(cfg), new Date().toISOString(), "utf8");
}

export function deleteSentinel(cfg: Config) {
  try { rmSync(sentinelPath(cfg)); } catch { /* already gone */ }
}

export async function launchGame(cfg: Config, waitMs: number): Promise<void> {
  const exe = exePath(cfg);
  if (!existsSync(exe)) throw new Error(`gta_sa.exe not found at: ${exe}`);

  createSentinel(cfg);

  // Spawn detached — game runs independently of MCP process
  Bun.spawn([exe], {
    cwd: cfg.gamePath,
    stdout: "ignore",
    stderr: "ignore",
    stdin: "ignore",
  });

  // Wait for process to appear
  const deadline = Date.now() + waitMs;
  while (Date.now() < deadline) {
    if (await isGameRunning()) return;
    await Bun.sleep(500);
  }
  throw new Error(`GTA SA did not start within ${waitMs}ms`);
}

export async function killGame(cfg: Config): Promise<void> {
  deleteSentinel(cfg);
  const pid = await findGamePid();
  if (!pid) return;
  try {
    Bun.spawn(["taskkill", "/PID", String(pid), "/F"], { stdout: "ignore", stderr: "ignore" });
  } catch { /* ignore */ }
}
