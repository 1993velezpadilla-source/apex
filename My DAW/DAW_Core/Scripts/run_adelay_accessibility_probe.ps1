param(
    [string] $Executable,
    [int] $ProcessId = 0,
    [Parameter(Mandatory = $true)] [string] $Fixture
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

function New-ControlTypeCondition($ControlType) {
    return New-Object -TypeName System.Windows.Automation.PropertyCondition -ArgumentList @(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        $ControlType)
}

function Get-RootWindows {
    $condition = New-ControlTypeCondition ([System.Windows.Automation.ControlType]::Window)
    return [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Children, $condition)
}

function Get-Buttons($Root) {
    $condition = New-ControlTypeCondition ([System.Windows.Automation.ControlType]::Button)
    return $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Invoke-Button($Button, [string] $Purpose) {
    $pattern = $null
    if (-not $Button.TryGetCurrentPattern(
        [System.Windows.Automation.InvokePattern]::Pattern, [ref] $pattern)) {
        throw "$Purpose does not expose InvokePattern"
    }

    ([System.Windows.Automation.InvokePattern] $pattern).Invoke()
    Write-Host "INVOKED=$Purpose"
}

function Wait-MainWindow($Process, [int] $TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "APEX exited before its main window: $($Process.ExitCode)"
        }

        foreach ($window in (Get-RootWindows)) {
            if ($window.Current.ProcessId -ne $Process.Id) { continue }
            $bounds = $window.Current.BoundingRectangle
            if ($bounds.Width -ge 900 -and $bounds.Height -ge 600) {
                return $window
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Timed out waiting for the stable APEX main window'
}

function Find-VisibleNamedButton($Root, [string] $Name) {
    foreach ($button in (Get-Buttons $Root)) {
        if ($button.Current.Name -ne $Name -or $button.Current.IsOffscreen) { continue }
        $bounds = $button.Current.BoundingRectangle
        if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) { return $button }
    }
    return $null
}

function Wait-VisibleNamedButton($Root, $Process, [string] $Name, [int] $TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw "APEX exited while waiting for $Name" }
        try {
            $button = Find-VisibleNamedButton $Root $Name
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable') { throw }
            $Root = Wait-MainWindow $Process 10
            $button = $null
        }
        if ($null -ne $button) { return $button }
        Start-Sleep -Milliseconds 100
    }
    throw "$Name accessibility control did not become visible"
}

function Try-Wait-VisibleNamedButton($Root, $Process, [string] $Name, [int] $TimeoutMilliseconds) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw "APEX exited while waiting for $Name" }
        try {
            $button = Find-VisibleNamedButton $Root $Name
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable') { throw }
            $Root = Wait-MainWindow $Process 10
            $button = $null
        }
        if ($null -ne $button) { return $button }
        Start-Sleep -Milliseconds 50
    }
    return $null
}

function Get-UiFlowLogPath {
    return [IO.Path]::Combine(
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData),
        'DAW_Core', 'plugin_ui_flow.log')
}

function Get-FileLengthOrZero([string] $Path) {
    if (-not [IO.File]::Exists($Path)) { return [int64] 0 }
    return (New-Object IO.FileInfo($Path)).Length
}

function Read-AppendedText([string] $Path, [int64] $Offset) {
    if (-not [IO.File]::Exists($Path)) { return '' }
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($Offset -gt $stream.Length) { $Offset = 0 }
        [void] $stream.Seek($Offset, [IO.SeekOrigin]::Begin)
        $reader = New-Object IO.StreamReader($stream)
        try { return $reader.ReadToEnd() }
        finally { $reader.Dispose() }
    }
    finally { $stream.Dispose() }
}

function Find-ButtonAtExpectedBounds($MainWindow, $Process, $ExpectedBounds, [double] $Tolerance) {
    $matches = @()
    foreach ($button in (Get-Buttons $MainWindow)) {
        if ($button.Current.ProcessId -ne $Process.Id -or $button.Current.IsOffscreen) { continue }
        $bounds = $button.Current.BoundingRectangle
        if ($bounds.Width -le 0 -or $bounds.Height -le 0) { continue }

        $actualCentreX = $bounds.X + ($bounds.Width / 2.0)
        $actualCentreY = $bounds.Y + ($bounds.Height / 2.0)
        $expectedCentreX = $ExpectedBounds.X + ($ExpectedBounds.Width / 2.0)
        $expectedCentreY = $ExpectedBounds.Y + ($ExpectedBounds.Height / 2.0)

        if ([Math]::Abs($actualCentreX - $expectedCentreX) -le $Tolerance -and
            [Math]::Abs($actualCentreY - $expectedCentreY) -le $Tolerance -and
            [Math]::Abs($bounds.Width - $ExpectedBounds.Width) -le $Tolerance -and
            [Math]::Abs($bounds.Height - $ExpectedBounds.Height) -le $Tolerance) {
            $matches += $button
        }
    }

    if ($matches.Count -eq 1) { return $matches[0] }
    if ($matches.Count -gt 1) {
        throw "PluginScanStartupDialog close geometry matched $($matches.Count) UIA buttons; refusing an ambiguous invocation"
    }

    # UIA providers can omit a child from a bulk descendant query while still
    # returning it from hit-testing.  The point is not guessed: it is the exact
    # centre derived below from PluginScanStartupDialog::resized().
    $point = New-Object System.Windows.Point(
        ($ExpectedBounds.X + ($ExpectedBounds.Width / 2.0)),
        ($ExpectedBounds.Y + ($ExpectedBounds.Height / 2.0)))
    $element = [System.Windows.Automation.AutomationElement]::FromPoint($point)
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    while ($null -ne $element) {
        if ($element.Current.ProcessId -ne $Process.Id) { break }
        if ($element.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button) {
            $bounds = $element.Current.BoundingRectangle
            $centreX = $bounds.X + ($bounds.Width / 2.0)
            $centreY = $bounds.Y + ($bounds.Height / 2.0)
            if ([Math]::Abs($centreX - $point.X) -le $Tolerance -and
                [Math]::Abs($centreY - $point.Y) -le $Tolerance) {
                return $element
            }
        }
        $element = $walker.GetParent($element)
    }

    return $null
}

function Wait-AndClose-PluginScanStartupDialog($MainWindow, $Process, [int] $TimeoutSeconds) {
    # "Rescan All" is a real child of PluginScanStartupDialog and does not
    # occur on StartupPanel (whose distinct control is "Rescan Plugins").
    $MainWindow = Wait-MainWindow $Process 10
    $rescanAll = Wait-VisibleNamedButton $MainWindow $Process 'Rescan All' $TimeoutSeconds
    # Overlay creation can invalidate a previously acquired JUCE UIA subtree.
    # Reacquire the top-level element and the geometry anchor before resolving
    # the close control.
    $MainWindow = Wait-MainWindow $Process 10
    $rescanAll = Wait-VisibleNamedButton $MainWindow $Process 'Rescan All' 10
    $rescanBounds = $rescanAll.Current.BoundingRectangle

    # Exact source layout in PluginScanStartupDialog::resized():
    #   rescan = { box.x + 20, box.bottom - 36, 90, 24 }
    #   close  = { box.right - 28, box.y + 8, 20, 20 }
    # Therefore close.left = rescan.left + 372 and close.top =
    # rescan.top - 136, scaled by the actual UIA control dimensions.
    $scaleX = $rescanBounds.Width / 90.0
    $scaleY = $rescanBounds.Height / 24.0
    if ($scaleX -le 0.0 -or $scaleY -le 0.0) {
        throw "Invalid PluginScanStartupDialog scale from Rescan All bounds: $rescanBounds"
    }

    $expected = New-Object System.Windows.Rect(
        ($rescanBounds.X + (372.0 * $scaleX)),
        ($rescanBounds.Y - (136.0 * $scaleY)),
        (20.0 * $scaleX),
        (20.0 * $scaleY))
    $tolerance = [Math]::Max(3.0, 3.0 * [Math]::Max($scaleX, $scaleY))
    $close = Find-ButtonAtExpectedBounds $MainWindow $Process $expected $tolerance
    if ($null -eq $close) {
        throw "PluginScanStartupDialog was detected through Rescan All, but its exact layout-derived Close button was not exposed by UIA; expected=$expected rescan=$rescanBounds"
    }

    $actual = $close.Current.BoundingRectangle
    Write-Host "PLUGIN_SCAN_DIALOG=detected rescanBounds=$rescanBounds"
    Write-Host "PLUGIN_SCAN_CLOSE=resolved name=`"$($close.Current.Name)`" expected=$expected actual=$actual"
    Invoke-Button $close 'PluginScanStartupDialog.Close'
}

function Ensure-ScanDialogAndClose($MainWindow, $Process) {
    # Prefer the launch overlay. Cached startup auto-closes it after 750 ms, so
    # if StartupPanel wins that race, open a deterministic scan cycle through
    # its real UIA control. Under full page heap the native window can exist for
    # many seconds before either JUCE subtree is ready, so keep observing both
    # valid states rather than committing to one after a fixed startup delay.
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(180)
    $scanDialogObserved = $false
    while ([DateTime]::UtcNow -lt $startupDeadline -and -not $scanDialogObserved) {
        $Process.Refresh()
        if ($Process.HasExited) { throw 'APEX exited before a startup UI state became available' }
        try {
            $rescanAll = Find-VisibleNamedButton $MainWindow 'Rescan All'
            if ($null -ne $rescanAll) {
                $scanDialogObserved = $true
                break
            }

            $openProject = Find-VisibleNamedButton $MainWindow 'Open Project'
            if ($null -ne $openProject) {
                Write-Host "STARTUP_PANEL_BEFORE_RESCAN=visible openProjectBounds=$($openProject.Current.BoundingRectangle)"
                $rescanPlugins = Find-VisibleNamedButton $MainWindow 'Rescan Plugins'
                if ($null -ne $rescanPlugins) {
                    Write-Host "RESCAN_PLUGINS_FOUND bounds=$($rescanPlugins.Current.BoundingRectangle)"
                    Invoke-Button $rescanPlugins 'StartupPanel.RescanPlugins'
                    $scanDialogObserved = $true
                    break
                }
            }
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable') { throw }
            $MainWindow = Wait-MainWindow $Process 10
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $scanDialogObserved) {
        throw 'Timed out before either PluginScanStartupDialog or the accessible StartupPanel became available'
    }

    $uiFlowLog = Get-UiFlowLogPath
    $uiFlowOffset = Get-FileLengthOrZero $uiFlowLog
    Wait-AndClose-PluginScanStartupDialog $MainWindow $Process 30
    $MainWindow = Wait-MainWindow $Process 10
    return New-Object PSObject -Property @{
        UiFlowLog = $uiFlowLog
        UiFlowOffset = [int64] $uiFlowOffset
        MainWindow = $MainWindow
    }
}

function Wait-ScanCompleteAndStartupPanel($MainWindow, $Process, [string] $UiFlowLog, [int64] $UiFlowOffset, [int] $TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $markerObserved = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw 'APEX exited before scan completion reached StartupPanel' }

        $appended = Read-AppendedText $UiFlowLog $UiFlowOffset
        if ($appended -match 'MainComponent::scanComplete') { $markerObserved = $true }

        try {
            $openProject = Find-VisibleNamedButton $MainWindow 'Open Project'
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable') { throw }
            $MainWindow = Wait-MainWindow $Process 10
            $openProject = $null
        }
        if ($markerObserved -and $null -ne $openProject) {
            Write-Host 'ON_SCAN_COMPLETE=observed_in_plugin_ui_flow_log'
            Write-Host "STARTUP_PANEL=visible openProjectBounds=$($openProject.Current.BoundingRectangle)"
            return $openProject
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for both a newly appended MainComponent::scanComplete marker and visible StartupPanel Open Project control; markerObserved=$markerObserved"
}

function Wait-FileDialog($Process, [int] $TimeoutSeconds) {
    $condition = New-ControlTypeCondition ([System.Windows.Automation.ControlType]::Window)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
                [System.Windows.Automation.TreeScope]::Children, $condition)
            foreach ($window in $windows) {
                if ($window.Current.ProcessId -ne $Process.Id) { continue }
                if ($window.Current.Name -eq 'Open Project') { return $window }

                # Depending on the common-dialog provider state, the native
                # #32770 window may be a desktop child or nested beneath the
                # JUCE top-level window in the UIA control view.
                $nestedWindows = $window.FindAll(
                    [System.Windows.Automation.TreeScope]::Descendants, $condition)
                foreach ($nested in $nestedWindows) {
                    if ($nested.Current.ProcessId -eq $Process.Id -and
                        $nested.Current.Name -eq 'Open Project') {
                        return $nested
                    }
                }
            }
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable|Could not open the process token|Operation timed out') {
                throw
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Open Project file dialog did not appear'
}

function Submit-FileDialog($Dialog, $Process, [string] $Path, [int] $TimeoutSeconds) {
    # The common dialog's HWND can appear before its UIA descendants are ready,
    # and querying that Shell subtree can time out under full page heap. Use the
    # documented native dialog control IDs already exposed as UIA AutomationIds
    # (filename=1148, Open=1). This targets the actual controls and never uses
    # coordinates.
    if ($null -eq ('ApexFileDialogNative' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ApexFileDialogNative {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, string lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw 'APEX exited before the Open Project dialog became ready' }
        try {
            $dialogHwnd = [IntPtr] $Dialog.Current.NativeWindowHandle
            $fileNameHwnd = [ApexFileDialogNative]::GetDlgItem($dialogHwnd, 1148)
            $openHwnd = [ApexFileDialogNative]::GetDlgItem($dialogHwnd, 1)
            if ($dialogHwnd -ne [IntPtr]::Zero -and
                [ApexFileDialogNative]::IsWindow($fileNameHwnd) -and
                [ApexFileDialogNative]::IsWindow($openHwnd) -and
                [ApexFileDialogNative]::IsWindowEnabled($openHwnd)) {
                [void] [ApexFileDialogNative]::SendMessage(
                    $fileNameHwnd, 0x000C, [IntPtr]::Zero, $Path) # WM_SETTEXT
                Write-Host "FILE_DIALOG_READY method=exact-native-control-ids editId=1148 openId=1"
                [void] [ApexFileDialogNative]::SendMessage(
                    $openHwnd, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) # BM_CLICK
                Write-Host 'INVOKED=FileDialog.Open'
                return
            }
        }
        catch {
            if ($_.Exception.ToString() -notmatch 'no longer available|ElementNotAvailable|Could not open the process token|Operation timed out') {
                throw
            }
            Start-Sleep -Milliseconds 200
            $Dialog = Wait-FileDialog $Process 10
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Open Project dialog appeared, but its filename edit and Open button did not become UIA-ready before timeout'
}

function Get-SessionLogDirectory {
    return [IO.Path]::Combine(
        [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments),
        'DAW_Core_Projects', 'Logs')
}

function Get-SessionLogLengths {
    $lengths = @{}
    $directory = Get-SessionLogDirectory
    if (-not [IO.Directory]::Exists($directory)) { return $lengths }
    foreach ($path in [IO.Directory]::GetFiles($directory, 'DAW_Core_Session_*.log')) {
        $lengths[$path] = (New-Object IO.FileInfo($path)).Length
    }
    return $lengths
}

function Wait-ADelaySuccess($BaselineLengths, [int] $TimeoutSeconds) {
    $directory = Get-SessionLogDirectory
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $candidates = @()
        if ([IO.Directory]::Exists($directory)) {
            foreach ($path in [IO.Directory]::GetFiles($directory, 'DAW_Core_Session_*.log')) {
                $info = New-Object IO.FileInfo($path)
                $offset = if ($BaselineLengths.ContainsKey($path)) { [int64] $BaselineLengths[$path] } else { [int64] 0 }
                $appended = Read-AppendedText $path $offset
                if ($appended -match '\[PLUGIN SAFETY\] creation failed[^\r\n]*name="ADelay"') {
                    throw "ADelay creation failed in newly appended session evidence: $path"
                }
                $match = [regex]::Match($appended, '\[PLUGIN SAFETY\] creation succeeded[^\r\n]*name="ADelay"[^\r\n]*')
                if ($match.Success) {
                    if ($match.Value -notmatch 'uniqueId=-19464422(?:\s|$)' -or
                        $match.Value -notmatch 'deprecatedUid=-1457048775(?:\s|$)') {
                        throw "ADelay creation succeeded with unexpected plugin IDs: $($match.Value)"
                    }
                    $candidates += New-Object PSObject -Property @{
                        Info = $info
                        Line = $match.Value
                        BaselineOffset = $offset
                    }
                }
            }
        }

        if ($candidates.Count -gt 0) {
            $selected = $candidates | Sort-Object { $_.Info.LastWriteTimeUtc } -Descending | Select-Object -First 1
            $newest = [IO.Directory]::GetFiles($directory, 'DAW_Core_Session_*.log') |
                ForEach-Object { New-Object IO.FileInfo($_) } |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
            if ($selected.Info.FullName -ne $newest.FullName) {
                throw "ADelay succeeded in $($selected.Info.FullName), but it is not the newest session log ($($newest.FullName))"
            }
            Write-Host "SESSION_LOG=$($selected.Info.FullName)"
            Write-Host "ADELAY_LOG_LINE=$($selected.Line)"
            Write-Host 'ADELAY_PLUGIN_IDS=uniqueId=-19464422 deprecatedUid=-1457048775'
            Write-Host 'ADELAY_CREATION=success'
            return $selected
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'Timed out waiting for explicit ADelay creation-success evidence in newly appended text of the newest session log'
}

function Find-VisibleProcessButton($Process, [string] $Name) {
    foreach ($window in (Get-RootWindows)) {
        if ($window.Current.ProcessId -ne $Process.Id) { continue }
        foreach ($button in (Get-Buttons $window)) {
            if ($button.Current.Name -eq $Name -and -not $button.Current.IsOffscreen) {
                $bounds = $button.Current.BoundingRectangle
                if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) { return $button }
            }
        }
    }
    return $null
}

function Close-MainWindow($MainWindow, $Process) {
    $pattern = $null
    if (-not $MainWindow.TryGetCurrentPattern(
        [System.Windows.Automation.WindowPattern]::Pattern, [ref] $pattern)) {
        throw 'APEX main window does not expose WindowPattern'
    }
    ([System.Windows.Automation.WindowPattern] $pattern).Close()
    Write-Host 'INVOKED=APEX.Window.Close'

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { return }
        $dontSave = Find-VisibleProcessButton $Process "Don't Save"
        if ($null -ne $dontSave) {
            Invoke-Button $dontSave 'QuitSafetyDialog.DontSave'
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'APEX neither exited nor exposed the expected quit-safety control after WindowPattern.Close'
}

if (-not [IO.File]::Exists($Fixture)) { throw "Fixture does not exist: $Fixture" }
$sessionLogBaselines = Get-SessionLogLengths

$startedByProbe = $ProcessId -le 0
if ($ProcessId -gt 0) {
    $process = Get-Process -Id $ProcessId
    "ATTACHED_PID=$($process.Id)"
} else {
    if ([string]::IsNullOrWhiteSpace($Executable)) {
        throw 'Executable is required when ProcessId is not supplied'
    }
    if (-not [IO.File]::Exists($Executable)) { throw "Executable does not exist: $Executable" }
    $process = Start-Process -FilePath $Executable -PassThru
    "STARTED_PID=$($process.Id)"
}

$mainWindow = Wait-MainWindow $process 180
"MAIN_WINDOW name=$($mainWindow.Current.Name) bounds=$($mainWindow.Current.BoundingRectangle)"

$scanEvidence = Ensure-ScanDialogAndClose $mainWindow $process
$mainWindow = $scanEvidence.MainWindow
$openProject = Wait-ScanCompleteAndStartupPanel $mainWindow $process $scanEvidence.UiFlowLog $scanEvidence.UiFlowOffset 30
"OPEN_PROJECT_CONFIRMED name=$($openProject.Current.Name) bounds=$($openProject.Current.BoundingRectangle)"
Invoke-Button $openProject 'StartupPanel.OpenProject'

$fileDialog = $null
try {
    $fileDialog = Wait-FileDialog $process 30
}
catch {
    if ($_.Exception.Message -notmatch 'Open Project file dialog did not appear') { throw }
    # Under full page heap the first asynchronous FileChooser launch can be
    # delayed or dropped while the Shell provider initializes. Reconfirm that
    # StartupPanel is still the visible authority and retry its actual UIA
    # control once; never synthesize a coordinate click.
    Write-Host 'FILE_DIALOG_FIRST_LAUNCH=not_observed_retrying_actual_open_control'
    $mainWindow = Wait-MainWindow $process 10
    $openProject = Wait-VisibleNamedButton $mainWindow $process 'Open Project' 30
    Write-Host "OPEN_PROJECT_RECONFIRMED name=$($openProject.Current.Name) bounds=$($openProject.Current.BoundingRectangle)"
    Invoke-Button $openProject 'StartupPanel.OpenProject.Retry'
    $fileDialog = Wait-FileDialog $process 180
}
Submit-FileDialog $fileDialog $process $Fixture 90
"FIXTURE_SUBMITTED=$Fixture"

$sessionEvidence = Wait-ADelaySuccess $sessionLogBaselines 90
$mainWindow = Wait-MainWindow $process 10
Close-MainWindow $mainWindow $process
if (-not $process.WaitForExit(120000)) { throw 'APEX did not exit after the normal close path' }
$exitCode = $process.ExitCode
if ($startedByProbe) {
    "APEX_EXIT=$exitCode"
    if ($null -eq $exitCode -or $exitCode -ne 0) {
        throw "APEX exited abnormally or without a queryable exit code: $exitCode"
    }
} else {
    # Get-Process attachment does not retain a queryable process handle after
    # exit on all PowerShell 5.1 builds. The CDB wrapper independently requires
    # debugger exit 0 and its normal-target-exit marker for attached runs.
    "APEX_EXIT=process_terminated_attached_exit_code_unavailable"
}

$postBaselineText = Read-AppendedText $sessionEvidence.Info.FullName ([int64] $sessionEvidence.BaselineOffset)
if ($postBaselineText -notmatch '\[SESSION\] DAW_Core clean shutdown') {
    throw "Clean shutdown marker missing from newly appended evidence in $($sessionEvidence.Info.FullName)"
}

$directory = Get-SessionLogDirectory
$newestFinal = [IO.Directory]::GetFiles($directory, 'DAW_Core_Session_*.log') |
    ForEach-Object { New-Object IO.FileInfo($_) } |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($sessionEvidence.Info.FullName -ne $newestFinal.FullName) {
    throw "Verified ADelay session log is no longer newest after shutdown: newest=$($newestFinal.FullName)"
}

'CLEAN_SHUTDOWN=confirmed'
