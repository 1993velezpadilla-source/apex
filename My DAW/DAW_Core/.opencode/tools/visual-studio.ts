import { execFile } from "node:child_process";
import { promisify } from "node:util";
import path from "node:path";
import { existsSync } from "node:fs";

const execFileAsync = promisify(execFile);

const BRIDGE_REL = "tools/VisualStudioBridge/bin/Release/net8.0/VisualStudioBridge.exe";

interface BridgeResult {
  Success: boolean;
  Error?: string;
  Data?: any;
}

interface ToolArgs {
  action: string;
  configuration?: string;
  platform?: string;
  file?: string;
  line?: string;
}

function findBridge(): string | null {
  // Try project root (cwd)
  const local = path.resolve(process.cwd(), BRIDGE_REL);
  if (existsSync(local)) return local;

  // Try relative to this file
  const here = path.resolve(
    import.meta.dirname ?? ".",
    "../../..",
    BRIDGE_REL
  );
  if (existsSync(here)) return here;

  return null;
}

async function runBridge(
  args: string[],
  timeoutMs = 30_000
): Promise<BridgeResult> {
  const bridge = findBridge();
  if (!bridge) {
    return {
      Success: false,
      Error:
        "VisualStudioBridge not found. Build with: dotnet build tools/VisualStudioBridge -c Release",
    };
  }

  try {
    const { stdout } = await execFileAsync(bridge, args, {
      timeout: timeoutMs,
      windowsHide: true,
      maxBuffer: 1024 * 1024 * 4,
    });

    const parsed = JSON.parse(stdout.trim()) as BridgeResult;
    return parsed;
  } catch (err: any) {
    if (err.killed) {
      return { Success: false, Error: `Bridge timed out after ${timeoutMs}ms` };
    }
    return { Success: false, Error: err.message ?? String(err) };
  }
}

export default {
  name: "visual_studio",
  description:
    "Control and query the running Visual Studio instance that has the APEX solution open. Actions: status, build, rebuild, start_debug, start_without_debug, pause, continue, stop, stack, output, crash_context, open_source.",
  parameters: {
    type: "object",
    properties: {
      action: {
        type: "string",
        description: "The action to perform",
        enum: [
          "status",
          "build",
          "rebuild",
          "start_debug",
          "start_without_debug",
          "pause",
          "continue",
          "stop",
          "stack",
          "output",
          "crash_context",
          "open_source",
        ],
      },
      configuration: {
        type: "string",
        description: "Build configuration: Debug or Release (default: Release)",
      },
      platform: {
        type: "string",
        description: "Build platform: x64, x86, or ARM64 (default: x64)",
      },
      file: {
        type: "string",
        description: "Absolute source file path (for open_source action)",
      },
      line: {
        type: "string",
        description: "Line number (for open_source action)",
      },
    },
    required: ["action"],
  },

  async execute(args: ToolArgs): Promise<string> {
    const { action } = args;

    // Map tool action names to bridge command names
    const cmdMap: Record<string, string> = {
      status: "status",
      build: "build",
      rebuild: "rebuild",
      start_debug: "start-debug",
      start_without_debug: "start-without-debug",
      pause: "pause",
      continue: "continue",
      stop: "stop",
      stack: "stack",
      output: "output-build", // default; agent can request output-debug via command
      crash_context: "crash-context",
      open_source: "open-source",
    };

    const bridgeCmd = cmdMap[action];
    if (!bridgeCmd) {
      return JSON.stringify({ error: `Unknown action: ${action}` });
    }

    // Build argument list
    const bridgeArgs: string[] = [bridgeCmd];

    if (action === "build" || action === "rebuild") {
      if (args.configuration) {
        bridgeArgs.push("--configuration", args.configuration);
      }
      if (args.platform) {
        bridgeArgs.push("--platform", args.platform);
      }
    }

    if (action === "open_source") {
      if (!args.file) {
        return JSON.stringify({ error: "open_source requires --file" });
      }
      bridgeArgs.push("--file", args.file);
      if (args.line) {
        bridgeArgs.push("--line", args.line);
      }
    }

    // Longer timeout for build commands
    const timeout =
      action === "build" || action === "rebuild" ? 300_000 : 30_000;

    const result = await runBridge(bridgeArgs, timeout);
    return JSON.stringify(result, null, 2);
  },
};
