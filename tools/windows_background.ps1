param(
    [switch]$Install,
    [switch]$StatusOnly,
    [switch]$StopOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$WorkerTaskName = "CanadaverseWDGMeshBridge"
$TrayTaskName = "CanadaverseWDGMeshTray"
$Root = Split-Path -Parent $PSScriptRoot
$Python = Join-Path $Root ".venv\Scripts\pythonw.exe"
$Bridge = Join-Path $PSScriptRoot "wdg_mesh_bridge.py"
$TrayHost = Join-Path $PSScriptRoot "windows_tray_host.vbs"

function Get-TaskState([string]$TaskName) {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($null -eq $task) { return "Missing" }
    return $task.State.ToString()
}

function Stop-BridgeWorker {
    $worker = Get-ScheduledTask -TaskName $WorkerTaskName -ErrorAction SilentlyContinue
    if ($null -eq $worker) { return }
    $arguments = @($worker.Actions)[0].Arguments
    if ($arguments -notmatch '^"([^"\r\n]+[\\/]wdg_mesh_bridge\.py)" run$') {
        throw "Unexpected bridge action; refusing to stop unrelated processes."
    }
    $bridgePattern = '"' + [regex]::Escape($Matches[1]) + '" run(?:\s|$)'
    # Windows venv launchers can leave their interpreter child alive after the
    # task stops. Select only workers running this task's exact bridge script.
    $workers = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('python.exe', 'pythonw.exe') -and
        $_.CommandLine -match $bridgePattern
    })
    Stop-ScheduledTask -TaskName $WorkerTaskName
    foreach ($workerProcess in $workers) {
        Stop-Process -Id $workerProcess.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

if (@($Install, $StatusOnly, $StopOnly).Where({ $_ }).Count -gt 1) {
    throw "Choose only one of -Install, -StatusOnly, or -StopOnly."
}

if ($StopOnly) {
    Stop-BridgeWorker
    Write-Output "Bridge: $(Get-TaskState $WorkerTaskName)"
    exit 0
}

if ($StatusOnly) {
    Write-Output "Bridge: $(Get-TaskState $WorkerTaskName)"
    Write-Output "Tray: $(Get-TaskState $TrayTaskName)"
    exit 0
}

if ($Install) {
    if (-not (Test-Path -LiteralPath $Python)) {
        throw "Python environment is missing; run setup_bridge.py first."
    }
    if (-not (Test-Path -LiteralPath $Bridge)) {
        throw "Bridge script is missing: $Bridge"
    }
    if (-not (Test-Path -LiteralPath $TrayHost)) {
        throw "Windowless tray host is missing: $TrayHost"
    }

    $account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $principal = New-ScheduledTaskPrincipal `
        -UserId $account -LogonType Interactive -RunLevel Limited
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $account
    $workerAction = New-ScheduledTaskAction `
        -Execute $Python -Argument ('"' + $Bridge + '" run') `
        -WorkingDirectory $Root
    $workerSettings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable -RestartCount 999 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit ([TimeSpan]::Zero) `
        -MultipleInstances IgnoreNew -Hidden

    $wscript = Join-Path $env:SystemRoot "System32\wscript.exe"
    $trayAction = New-ScheduledTaskAction `
        -Execute $wscript `
        -Argument ('//B //NoLogo "' + $TrayHost + '" "' + $PSCommandPath + '"') `
        -WorkingDirectory $Root
    $traySettings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) `
        -MultipleInstances IgnoreNew -Hidden

    foreach ($taskName in @($WorkerTaskName, $TrayTaskName)) {
        if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
            if ($taskName -eq $WorkerTaskName) { Stop-BridgeWorker }
            else { Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue }
        }
    }

    Register-ScheduledTask -TaskName $WorkerTaskName -Force `
        -Action $workerAction -Trigger $trigger -Principal $principal `
        -Settings $workerSettings `
        -Description "Live USB MeshCore GPS advert bridge to WDG; the API key remains in Windows Credential Manager." | Out-Null
    Register-ScheduledTask -TaskName $TrayTaskName -Force `
        -Action $trayAction -Trigger $trigger -Principal $principal `
        -Settings $traySettings `
        -Description "System-tray start/stop control for the Canadaverse WDG Mesh Bridge." | Out-Null

    Start-ScheduledTask -TaskName $WorkerTaskName
    Start-ScheduledTask -TaskName $TrayTaskName
    Start-Sleep -Seconds 2
    Write-Output "Bridge: $(Get-TaskState $WorkerTaskName)"
    Write-Output "Tray: $(Get-TaskState $TrayTaskName)"
    exit 0
}

if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") {
    throw "The tray must run in an STA PowerShell process."
}

$createdNew = $false
$mutex = [Threading.Mutex]::new(
    $true, "Local\CanadaverseWDGMeshTray", [ref]$createdNew
)
if (-not $createdNew) {
    $mutex.Dispose()
    exit 0
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$script:Notify = [Windows.Forms.NotifyIcon]::new()
$script:Menu = [Windows.Forms.ContextMenuStrip]::new()
$script:StatusItem = [Windows.Forms.ToolStripMenuItem]::new("Bridge: checking")
$script:StartItem = [Windows.Forms.ToolStripMenuItem]::new("Start bridge")
$script:StopItem = [Windows.Forms.ToolStripMenuItem]::new("Stop bridge")
$script:RestartItem = [Windows.Forms.ToolStripMenuItem]::new("Restart bridge")
$script:ExitItem = [Windows.Forms.ToolStripMenuItem]::new("Exit tray icon")
$script:Timer = [Windows.Forms.Timer]::new()

$script:StatusItem.Enabled = $false
$script:Menu.Items.Add($script:StatusItem) | Out-Null
$script:Menu.Items.Add([Windows.Forms.ToolStripSeparator]::new()) | Out-Null
$script:Menu.Items.Add($script:StartItem) | Out-Null
$script:Menu.Items.Add($script:StopItem) | Out-Null
$script:Menu.Items.Add($script:RestartItem) | Out-Null
$script:Menu.Items.Add([Windows.Forms.ToolStripSeparator]::new()) | Out-Null
$script:Menu.Items.Add($script:ExitItem) | Out-Null
$script:Notify.ContextMenuStrip = $script:Menu

function Update-Tray {
    $state = Get-TaskState $WorkerTaskName
    $script:StatusItem.Text = "Bridge: $state"
    $script:Notify.Text = "WDG Mesh Bridge: $state"
    $script:StartItem.Enabled = $state -notin @("Running", "Missing")
    $script:StopItem.Enabled = $state -eq "Running"
    $script:RestartItem.Enabled = $state -ne "Missing"
    $script:Notify.Icon = switch ($state) {
        "Running" { [Drawing.SystemIcons]::Information; break }
        "Missing" { [Drawing.SystemIcons]::Error; break }
        default { [Drawing.SystemIcons]::Warning }
    }
}

function Invoke-WorkerAction([string]$Action) {
    try {
        switch ($Action) {
            "Start" { Start-ScheduledTask -TaskName $WorkerTaskName }
            "Stop" { Stop-BridgeWorker }
            "Restart" {
                Stop-BridgeWorker
                Start-Sleep -Milliseconds 300
                Start-ScheduledTask -TaskName $WorkerTaskName
            }
        }
        Start-Sleep -Milliseconds 300
        Update-Tray
    }
    catch {
        $script:Notify.ShowBalloonTip(
            4000, "WDG Mesh Bridge", $_.Exception.Message,
            [Windows.Forms.ToolTipIcon]::Error
        )
    }
}

$script:StartItem.add_Click({ Invoke-WorkerAction "Start" })
$script:StopItem.add_Click({ Invoke-WorkerAction "Stop" })
$script:RestartItem.add_Click({ Invoke-WorkerAction "Restart" })
$script:ExitItem.add_Click({ [Windows.Forms.Application]::ExitThread() })
$script:Menu.add_Opening({ Update-Tray })
$script:Notify.add_DoubleClick({
    Update-Tray
    $script:Notify.ShowBalloonTip(
        2500, "WDG Mesh Bridge", $script:StatusItem.Text,
        [Windows.Forms.ToolTipIcon]::Info
    )
})
$script:Timer.Interval = 2000
$script:Timer.add_Tick({ Update-Tray })

try {
    Update-Tray
    $script:Notify.Visible = $true
    $script:Timer.Start()
    [Windows.Forms.Application]::Run()
}
finally {
    $script:Timer.Stop()
    $script:Notify.Visible = $false
    $script:Timer.Dispose()
    $script:Notify.Dispose()
    $script:Menu.Dispose()
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
