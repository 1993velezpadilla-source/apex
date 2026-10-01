using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;

namespace VisualStudioBridge;

class Program
{
    const string APEX_SolutionMarker = "DAW_Core.sln";
    const string APEX_SolutionPath = @"My DAW\DAW_Core";
    static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            WriteResult(new BridgeResult { Success = false, Error = "No command. Usage: VisualStudioBridge.exe <command> [opts]" });
            return 1;
        }

        var command = args[0].ToLowerInvariant();
        var options = ParseOptions(args.Skip(1).ToArray());

        try
        {
            dynamic? dte = FindApexVisualStudio();
            if (dte == null)
            {
                WriteResult(new BridgeResult
                {
                    Success = false,
                    Error = "No Visual Studio instance found with the APEX solution loaded. " +
                            "Ensure Visual Studio is open with DAW_Core.sln."
                });
                return 1;
            }

            return command switch
            {
                "status" => CmdStatus(dte),
                "build" => CmdBuild(dte, options, false),
                "rebuild" => CmdBuild(dte, options, true),
                "start-debug" => CmdExecCommand(dte, "Debug.Start", "start-debug", "Debug session started (F5)"),
                "start-without-debug" => CmdExecCommand(dte, "Debug.StartWithoutDebugging", "start-without-debug", "Started without debugging (Ctrl+F5)"),
                "pause" => CmdPause(dte),
                "continue" => CmdContinue(dte),
                "stop" => CmdStop(dte),
                "stack" => CmdStack(dte),
                "output-build" => CmdOutput(dte, "Build", "output-build"),
                "output-debug" => CmdOutput(dte, "Debug", "output-debug"),
                "crash-context" => CmdCrashContext(dte),
                "open-source" => CmdOpenSource(dte, options),
                _ => WriteErrorReturn($"Unknown command: {command}")
            };
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Bridge error: {ex.Message}" });
            return 1;
        }
    }

    // ── VS Discovery ───────────────────────────────────────────────────

    static dynamic? FindApexVisualStudio()
    {
        // Scan the Running Object Table for any VisualStudio.DTE instance
        return FindViaRot();
    }

    static bool HasApexSolution(object dte)
    {
        try
        {
            dynamic d = dte;
            var sol = (string)d.Solution.FullName;
            if (string.IsNullOrEmpty(sol)) return false;
            return sol.Contains(APEX_SolutionPath, StringComparison.OrdinalIgnoreCase) ||
                   sol.Contains(APEX_SolutionMarker, StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    static dynamic? FindViaRot()
    {
        try
        {
            var rot = GetRunningObjectTable();
            if (rot == null) return null;

            IEnumMoniker? enumMoniker = null;
            rot.EnumRunning(out enumMoniker);
            if (enumMoniker == null) return null;

            var fetched = IntPtr.Zero;
            var monikers = new IMoniker[1];
            enumMoniker.Next(1, monikers, fetched);
            while (Marshal.ReadInt32(fetched) > 0)
            {
                try
                {
                    object? obj = null;
                    rot.GetObject(monikers[0], out obj);
                    if (obj != null && HasApexSolution(obj)) return obj;
                }
                catch { }
                enumMoniker.Next(1, monikers, fetched);
            }
        }
        catch { }
        return null;
    }

    [DllImport("ole32.dll")]
    static extern int GetRunningObjectTable(uint reserved, out IRunningObjectTable pprot);

    static IRunningObjectTable? GetRunningObjectTable()
    {
        var hr = GetRunningObjectTable(0, out var rot);
        return hr == 0 ? rot : null;
    }

    // ── Status ─────────────────────────────────────────────────────────

    static int CmdStatus(dynamic dte)
    {
        var mode = GetDebugMode(dte);
        var exInfo = mode == "Break" ? GetExceptionInfo(dte) : null;
        WriteResult(new BridgeResult
        {
            Success = true,
            Data = new StatusData
            {
                Command = "status",
                DebuggerMode = mode,
                Solution = Try(() => (string)dte.Solution.FullName),
                Configuration = GetActiveConfig(dte),
                Platform = GetActivePlatform(dte),
                IsDebugging = mode != "Design",
                ExceptionInfo = exInfo
            }
        });
        return 0;
    }

    // ── Build / Rebuild ────────────────────────────────────────────────

    static int CmdBuild(dynamic dte, Dictionary<string, string> opts, bool rebuild)
    {
        var config = opts.GetValueOrDefault("configuration", "Release");
        var platform = opts.GetValueOrDefault("platform", "x64");
        var cfgPlat = $"{config}|{platform}";
        var sw = Stopwatch.StartNew();

        var buildText = GetOutputText(dte, "Build");

        try
        {
            var solBuild = dte.Solution.SolutionBuild;
            var configs = new object[] { cfgPlat };

            if (rebuild)
                solBuild.Rebuild(true, configs);
            else
                solBuild.Build(configs);

            // Wait up to 300s
            for (int i = 0; i < 600; i++)
            {
                var state = (int)solBuild.BuildState;
                if (state != 1) break; // 1 = vsBuildStateInProgress
                Thread.Sleep(500);
            }
            sw.Stop();

            // Re-read build output
            buildText = GetOutputText(dte, "Build");
            var debugText = GetOutputText(dte, "Debug");
            var failed = (int)solBuild.LastBuildInfo;
            var success = failed == 0;

            WriteResult(new BridgeResult
            {
                Success = success,
                Error = success ? null : $"Build failed: {failed} project(s)",
                Data = new BuildData
                {
                    Command = rebuild ? "rebuild" : "build",
                    Configuration = config,
                    Platform = platform,
                    Success = success,
                    FailedProjects = failed,
                    ElapsedMs = (int)sw.ElapsedMilliseconds,
                    BuildOutput = buildText,
                    DebugOutput = debugText
                }
            });
            return success ? 0 : 1;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Build error: {ex.Message}" });
            return 1;
        }
    }

    // ── Debugger Commands ──────────────────────────────────────────────

    static int CmdExecCommand(dynamic dte, string vsCmd, string label, string msg)
    {
        try
        {
            dte.ExecuteCommand(vsCmd);
            Thread.Sleep(1000);
            var mode = GetDebugMode(dte);
            WriteResult(new BridgeResult
            {
                Success = true,
                Data = new DebuggerActionData { Command = label, DebuggerMode = mode, Message = msg }
            });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Command '{label}' failed: {ex.Message}" });
            return 1;
        }
    }

    static int CmdPause(dynamic dte)
    {
        var mode = GetDebugMode(dte);
        if (mode != "Run")
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Cannot pause: mode is {mode}, not Run" });
            return 1;
        }
        try
        {
            dte.ExecuteCommand("Debug.Break");
            Thread.Sleep(500);
            var newMode = GetDebugMode(dte);
            WriteResult(new BridgeResult
            {
                Success = newMode == "Break",
                Data = new DebuggerActionData { Command = "pause", DebuggerMode = newMode, Message = "Execution paused" }
            });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Pause failed: {ex.Message}" });
            return 1;
        }
    }

    static int CmdContinue(dynamic dte)
    {
        var mode = GetDebugMode(dte);
        if (mode != "Break")
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Cannot continue: mode is {mode}, not Break" });
            return 1;
        }
        try
        {
            dte.ExecuteCommand("Debug.Continue");
            Thread.Sleep(500);
            var newMode = GetDebugMode(dte);
            WriteResult(new BridgeResult
            {
                Success = newMode == "Run",
                Data = new DebuggerActionData { Command = "continue", DebuggerMode = newMode, Message = "Execution resumed" }
            });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Continue failed: {ex.Message}" });
            return 1;
        }
    }

    static int CmdStop(dynamic dte)
    {
        var mode = GetDebugMode(dte);
        if (mode == "Design")
        {
            WriteResult(new BridgeResult
            {
                Success = true,
                Data = new DebuggerActionData { Command = "stop", DebuggerMode = "Design", Message = "Already in Design mode" }
            });
            return 0;
        }
        try
        {
            dte.ExecuteCommand("Debug.Stop");
            Thread.Sleep(1000);
            var newMode = GetDebugMode(dte);
            WriteResult(new BridgeResult
            {
                Success = newMode == "Design",
                Data = new DebuggerActionData { Command = "stop", DebuggerMode = newMode, Message = "Debugging stopped" }
            });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Stop failed: {ex.Message}" });
            return 1;
        }
    }

    // ── Stack ──────────────────────────────────────────────────────────

    static int CmdStack(dynamic dte)
    {
        var mode = GetDebugMode(dte);
        if (mode != "Break")
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Cannot get stack: mode is {mode}, not Break" });
            return 1;
        }
        try
        {
            var frames = GetCallStack(dte);
            var cur = GetCurrentFrame(dte);
            var ex = GetExceptionInfo(dte);
            WriteResult(new BridgeResult
            {
                Success = true,
                Data = new StackData
                {
                    Command = "stack",
                    DebuggerMode = mode,
                    CurrentFrame = cur,
                    CallStack = frames,
                    ExceptionInfo = ex,
                    ProcessId = Try(() => (int)dte.Debugger.CurrentProcess.ProcessID),
                    ProcessName = Try(() => (string)dte.Debugger.CurrentProcess.Name),
                    ThreadId = Try(() => (int)dte.Debugger.CurrentThread.ID),
                    ThreadName = Try(() => (string)dte.Debugger.CurrentThread.Name)
                }
            });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Stack failed: {ex.Message}" });
            return 1;
        }
    }

    // ── Output ─────────────────────────────────────────────────────────

    static int CmdOutput(dynamic dte, string paneName, string label)
    {
        var text = GetOutputText(dte, paneName);
        WriteResult(new BridgeResult
        {
            Success = true,
            Data = new OutputData { Command = label, Pane = paneName, Text = text, Length = text?.Length ?? 0 }
        });
        return 0;
    }

    // ── Crash Context ──────────────────────────────────────────────────

    static int CmdCrashContext(dynamic dte)
    {
        try
        {
            var mode = GetDebugMode(dte);
            var ex = GetExceptionInfo(dte);
            var frames = GetCallStack(dte);
            var cur = GetCurrentFrame(dte);
            var buildOut = GetOutputText(dte, "Build");
            var debugOut = GetOutputText(dte, "Debug");

            var ctx = new CrashContextData
            {
                Command = "crash-context",
                Timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz"),
                DebuggerMode = mode,
                IsException = ex != null,
                ExceptionInfo = ex,
                CurrentFrame = cur,
                CallStack = frames,
                ProcessId = Try(() => (int)dte.Debugger.CurrentProcess.ProcessID),
                ProcessName = Try(() => (string)dte.Debugger.CurrentProcess.Name),
                ThreadId = Try(() => (int)dte.Debugger.CurrentThread.ID),
                ThreadName = Try(() => (string)dte.Debugger.CurrentThread.Name),
                Configuration = GetActiveConfig(dte),
                Platform = GetActivePlatform(dte),
                BuildOutput = buildOut,
                DebugOutput = debugOut,
                Solution = Try(() => (string)dte.Solution.FullName)
            };

            WriteCrashFiles(ctx);
            WriteResult(new BridgeResult { Success = true, Data = ctx });
            return 0;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Crash context failed: {ex.Message}" });
            return 1;
        }
    }

    // ── Open Source ────────────────────────────────────────────────────

    static int CmdOpenSource(dynamic dte, Dictionary<string, string> opts)
    {
        var file = opts.GetValueOrDefault("file", "");
        var lineStr = opts.GetValueOrDefault("line", "1");
        if (string.IsNullOrEmpty(file))
        {
            WriteResult(new BridgeResult { Success = false, Error = "Missing --file argument" });
            return 1;
        }
        if (!int.TryParse(lineStr, out var line))
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Invalid line: {lineStr}" });
            return 1;
        }
        try
        {
            var window = dte.ItemOperations.OpenFile(file);
            if (window != null)
            {
                dynamic sel = window.Document.Selection;
                sel.MoveToLineAndOffset(line, 1);
                window.Activate();
                WriteResult(new BridgeResult
                {
                    Success = true,
                    Data = new OpenSourceData { Command = "open-source", File = file, Line = line, Message = $"Opened {Path.GetFileName(file)} at line {line}" }
                });
                return 0;
            }
            WriteResult(new BridgeResult { Success = false, Error = $"Failed to open: {file}" });
            return 1;
        }
        catch (Exception ex)
        {
            WriteResult(new BridgeResult { Success = false, Error = $"Open source failed: {ex.Message}" });
            return 1;
        }
    }

    // ── Helpers ────────────────────────────────────────────────────────

    static string GetDebugMode(dynamic dte)
    {
        try
        {
            var mode = (int)dte.Debugger.CurrentMode;
            return mode switch
            {
                0 => "Design",
                1 => "Run",
                2 => "Break",
                _ => "Unknown"
            };
        }
        catch { return "Unknown"; }
    }

    static string? GetActiveConfig(dynamic dte)
    {
        try
        {
            var name = (string)dte.Solution.SolutionBuild.ActiveConfiguration.Name;
            return name.Split('|')[0].Trim();
        }
        catch { return null; }
    }

    static string? GetActivePlatform(dynamic dte)
    {
        try
        {
            var name = (string)dte.Solution.SolutionBuild.ActiveConfiguration.Name;
            return name.Split('|')[1].Trim();
        }
        catch { return null; }
    }

    static ExceptionInfo? GetExceptionInfo(dynamic dte)
    {
        try
        {
            var ex = dte.Debugger.CurrentException;
            if (ex == null) return null;
            return new ExceptionInfo
            {
                Type = Try(() => (string)ex.Type),
                Description = Try(() => (string)ex.Description),
                Code = Try(() => (string)ex.Code)
            };
        }
        catch { return null; }
    }

    static FrameInfo? GetCurrentFrame(dynamic dte)
    {
        try
        {
            var f = dte.Debugger.CurrentStackFrame;
            if (f == null) return null;
            return new FrameInfo
            {
                Function = Try(() => (string)f.FunctionName),
                File = Try(() => (string)f.FileName),
                Line = Try(() => (int)f.LineNumber),
                Language = Try(() => (string)f.Language)
            };
        }
        catch { return null; }
    }

    static List<FrameInfo> GetCallStack(dynamic dte)
    {
        var list = new List<FrameInfo>();
        try
        {
            var top = dte.Debugger.CurrentStackFrame;
            if (top == null) return list;
            var col = top.Collection;
            if (col == null) return list;

            foreach (object item in col)
            {
                dynamic f = item;
                list.Add(new FrameInfo
                {
                    Function = Try(() => (string)f.FunctionName),
                    File = Try(() => (string)f.FileName),
                    Line = Try(() => (int)f.LineNumber),
                    Language = Try(() => (string)f.Language)
                });
            }
        }
        catch { }
        return list;
    }

    static string? GetOutputText(dynamic dte, string paneName)
    {
        try
        {
            var outWin = dte.Windows.Item(EnvDTE.Constants.vsWindowKindOutput);
            dynamic outputObj = outWin.Object;
            foreach (dynamic pane in outputObj.OutputWindowPanes)
            {
                if ((string)pane.Name == paneName)
                {
                    return pane.TextDocument?.Selection?.Text;
                }
            }
        }
        catch { }
        return null;
    }

    static void WriteCrashFiles(CrashContextData ctx)
    {
        try
        {
            var solDir = Path.GetDirectoryName(ctx.Solution);
            var debugDir = Path.Combine(solDir ?? ".", ".apex-debug");
            Directory.CreateDirectory(debugDir);

            File.WriteAllText(Path.Combine(debugDir, "latest-crash.json"),
                JsonSerializer.Serialize(ctx, JsonOpts), Encoding.UTF8);
            File.WriteAllText(Path.Combine(debugDir, "latest-crash.md"),
                FormatCrashMarkdown(ctx), Encoding.UTF8);
        }
        catch { }
    }

    static string FormatCrashMarkdown(CrashContextData ctx)
    {
        var sb = new StringBuilder();
        sb.AppendLine("# APEX Crash Report\n");
        sb.AppendLine($"**Timestamp:** {ctx.Timestamp}");
        sb.AppendLine($"**Debugger Mode:** {ctx.DebuggerMode}");
        sb.AppendLine($"**Solution:** {ctx.Solution}\n");

        if (ctx.ExceptionInfo != null)
        {
            sb.AppendLine("## Exception\n");
            sb.AppendLine($"- **Type:** {ctx.ExceptionInfo.Type}");
            sb.AppendLine($"- **Message:** {ctx.ExceptionInfo.Description}");
            sb.AppendLine($"- **Code:** {ctx.ExceptionInfo.Code}\n");
        }
        if (ctx.CurrentFrame != null)
        {
            sb.AppendLine("## Current Frame\n");
            sb.AppendLine($"- **Function:** {ctx.CurrentFrame.Function}");
            sb.AppendLine($"- **File:** {ctx.CurrentFrame.File}");
            sb.AppendLine($"- **Line:** {ctx.CurrentFrame.Line}\n");
        }
        sb.AppendLine("## Process\n");
        sb.AppendLine($"- **PID:** {ctx.ProcessId}");
        sb.AppendLine($"- **Process:** {ctx.ProcessName}");
        sb.AppendLine($"- **Thread:** {ctx.ThreadId} ({ctx.ThreadName})\n");

        if (ctx.CallStack.Count > 0)
        {
            sb.AppendLine("## Call Stack\n");
            for (int i = 0; i < ctx.CallStack.Count; i++)
            {
                var f = ctx.CallStack[i];
                var loc = !string.IsNullOrEmpty(f.File) ? $" ({f.File}:{f.Line})" : "";
                sb.AppendLine($"{i}: {f.Function}{loc}");
            }
            sb.AppendLine();
        }
        sb.AppendLine("## Configuration\n");
        sb.AppendLine($"- **Config:** {ctx.Configuration}");
        sb.AppendLine($"- **Platform:** {ctx.Platform}\n");

        if (!string.IsNullOrEmpty(ctx.BuildOutput))
        {
            sb.AppendLine("## Build Output\n\n```\n" + ctx.BuildOutput + "\n```\n");
        }
        if (!string.IsNullOrEmpty(ctx.DebugOutput))
        {
            sb.AppendLine("## Debug Output\n\n```\n" + ctx.DebugOutput + "\n```\n");
        }
        return sb.ToString();
    }

    static T? Try<T>(Func<T> fn)
    {
        try { return fn(); } catch { return default; }
    }

    static void WriteResult(BridgeResult r)
    {
        Console.WriteLine(JsonSerializer.Serialize(r, JsonOpts));
    }

    static int WriteErrorReturn(string msg)
    {
        WriteResult(new BridgeResult { Success = false, Error = msg });
        return 1;
    }

    static Dictionary<string, string> ParseOptions(string[] args)
    {
        var d = new Dictionary<string, string>();
        for (int i = 0; i < args.Length; i++)
            if (args[i].StartsWith("--") && i + 1 < args.Length)
                d[args[i][2..]] = args[++i];
        return d;
    }
}

// ── Data Models ───────────────────────────────────────────────────────

class BridgeResult
{
    public bool Success { get; set; }
    public string? Error { get; set; }
    public object? Data { get; set; }
}

class StatusData
{
    public string Command { get; set; } = "";
    public string DebuggerMode { get; set; } = "";
    public string? Solution { get; set; }
    public string? Configuration { get; set; }
    public string? Platform { get; set; }
    public bool IsDebugging { get; set; }
    public ExceptionInfo? ExceptionInfo { get; set; }
}

class BuildData
{
    public string Command { get; set; } = "";
    public string Configuration { get; set; } = "";
    public string Platform { get; set; } = "";
    public bool Success { get; set; }
    public int FailedProjects { get; set; }
    public int ElapsedMs { get; set; }
    public string? BuildOutput { get; set; }
    public string? DebugOutput { get; set; }
}

class DebuggerActionData
{
    public string Command { get; set; } = "";
    public string DebuggerMode { get; set; } = "";
    public string Message { get; set; } = "";
}

class StackData
{
    public string Command { get; set; } = "";
    public string DebuggerMode { get; set; } = "";
    public FrameInfo? CurrentFrame { get; set; }
    public List<FrameInfo> CallStack { get; set; } = new();
    public ExceptionInfo? ExceptionInfo { get; set; }
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = "";
    public int ThreadId { get; set; }
    public string ThreadName { get; set; } = "";
}

class OutputData
{
    public string Command { get; set; } = "";
    public string Pane { get; set; } = "";
    public string? Text { get; set; }
    public int Length { get; set; }
}

class CrashContextData
{
    public string Command { get; set; } = "";
    public string Timestamp { get; set; } = "";
    public string DebuggerMode { get; set; } = "";
    public bool IsException { get; set; }
    public ExceptionInfo? ExceptionInfo { get; set; }
    public FrameInfo? CurrentFrame { get; set; }
    public List<FrameInfo> CallStack { get; set; } = new();
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = "";
    public int ThreadId { get; set; }
    public string ThreadName { get; set; } = "";
    public string? Configuration { get; set; }
    public string? Platform { get; set; }
    public string? BuildOutput { get; set; }
    public string? DebugOutput { get; set; }
    public string? Solution { get; set; }
}

class OpenSourceData
{
    public string Command { get; set; } = "";
    public string File { get; set; } = "";
    public int Line { get; set; }
    public string Message { get; set; } = "";
}

class ExceptionInfo
{
    public string? Type { get; set; }
    public string? Description { get; set; }
    public string? Code { get; set; }
}

class FrameInfo
{
    public string? Function { get; set; }
    public string? File { get; set; }
    public int Line { get; set; }
    public string? Language { get; set; }
}

// COM Interop types use System.Runtime.InteropServices.ComTypes
