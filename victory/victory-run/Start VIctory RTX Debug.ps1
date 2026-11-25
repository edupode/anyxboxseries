param(
    [ValidateRange(1,3600)][int] $Seconds = 180,
    [ValidatePattern('^[0-9a-fA-F]{16}$')][string] $SkipShader,
    [ValidateSet(500,2500)][int] $PipelineWaitMs,
    [switch] $WorldPreview
)
$ErrorActionPreference = 'Stop'
$Host.UI.RawUI.WindowTitle = 'VIctory Series X RTX diagnostic'
$package = 'C:\Users\edupo\Downloads\GTA6.Xbox.v1.0.2.1-CYBERLEEK'
$title = Join-Path $package 'game_scarlett_master.exe'
$stage = $PSScriptRoot
$control = Join-Path $stage 'control'
$emulator = Join-Path $stage 'app\victory.exe'
$controller = Join-Path $control 'appcontainer-diagnostic.exe'
$log = Join-Path $control 'launch.log'
$controllerLog = Join-Path $control 'controller.log'
$status = Join-Path $control 'status.txt'

function Get-AccessRules([string] $Path) {
    $section = [Security.AccessControl.AccessControlSections]::Access
    $security = Get-Acl -LiteralPath $Path
    return $security.GetSecurityDescriptorSddlForm($section)
}

$runLock = [IO.File]::Open((Join-Path $control 'run.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
$started = $null
$previousSkip = $env:VICTORY_SKIP_PIPELINES
$previousWait = $env:VICTORY_PIPELINE_WAIT_MS
$previousPreview = $env:VICTORY_SCENE_PREVIEW
try {
    $previousLogs = @($status, $controllerLog, $log, (Join-Path $control 'memory-trend.log')) |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
    if ($previousLogs) {
        $archive = Join-Path $control ('previous-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssffffZ'))
        New-Item -ItemType Directory -Path $archive | Out-Null
        Copy-Item -LiteralPath $previousLogs -Destination $archive
    }

    $started = [DateTime]::UtcNow
    Set-Content -LiteralPath $status -Value @("started_utc=$($started.ToString('o'))", "timeout_seconds=$Seconds")
    if ($SkipShader) { Add-Content -LiteralPath $status -Value "skip_shader=$SkipShader" }
    if ($PSBoundParameters.ContainsKey('PipelineWaitMs') -or $WorldPreview) {
        if (!$PSBoundParameters.ContainsKey('PipelineWaitMs')) { $PipelineWaitMs = 500 }
        $controller = Join-Path $control 'appcontainer-scene.exe'
        $env:VICTORY_PIPELINE_WAIT_MS = [string]$PipelineWaitMs
        $env:VICTORY_SCENE_PREVIEW = if ($WorldPreview) { '1' } else { '0' }
        $capturePath = if ($WorldPreview) { Join-Path $stage 'state\scene-preview' } else { Join-Path $stage 'state\scene-probe' }
        New-Item -ItemType Directory -Path $capturePath -Force | Out-Null
        Add-Content -LiteralPath $status -Value "requested_pipeline_wait_ms=$PipelineWaitMs"
        Add-Content -LiteralPath $status -Value "world_preview=$($WorldPreview.IsPresent)"
    }
    foreach ($required in @($title, $emulator, $controller)) {
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing launch input: $required" }
    }
    if ((Resolve-Path -LiteralPath $package).Path -ne $package) { throw 'The game folder resolved to a different path.' }
    $other = @(Get-Process -Name 'victory*','appcontainer*' -ErrorAction SilentlyContinue)
    if ($other.Count) { throw 'Close the other VIctory run before launching a diagnostic run.' }
    if (Get-ChildItem -LiteralPath $package -Force -Recurse -Attributes ReparsePoint) {
        throw 'The game folder contains a reparse point.'
    }

    $paths = @($package) + @(Get-ChildItem -LiteralPath $package -Force -Recurse | ForEach-Object FullName)
    $before = @{}
    foreach ($path in $paths) { $before[$path] = Get-AccessRules $path }

    Write-Host 'Starting VIctory fxceless 2119b0e with the local diagnostic options. Series X, RTX rendering with device removal logging.'
    if ($SkipShader) { Write-Host "Diagnostic: skip compute pipeline $SkipShader." }
    if ($Seconds -gt 0) {
        Write-Host "The observation limit is $Seconds seconds."
    } else {
        Write-Host 'Close the game window when you finish. This console records memory and any crash.'
    }
    Write-Host "Runtime log: $log"
    Write-Host ''
    $env:VICTORY_SKIP_PIPELINES = $SkipShader
    $ErrorActionPreference = 'Continue'
    & $controller $stage $package $Seconds $emulator run $title --execute --console series-x 2>&1 |
        ForEach-Object { $_.ToString() } | Tee-Object -FilePath $controllerLog
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'

    $changedCount = 0
    foreach ($path in $paths) {
        if ((Get-AccessRules $path) -ne $before[$path]) { $changedCount++ }
    }
    Add-Content -LiteralPath $status -Value @("exit_code=$code", "package_acl_paths=$($paths.Count)", "package_acl_differences=$changedCount")
    if ($changedCount) { Write-Warning "$changedCount game folder permission records differ after the diagnostic." }
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        Write-Host ''
        Get-Content -LiteralPath $log -Tail 24
        if ($code -ne 0 -and $code -ne 124) {
            Start-Process -FilePath (Join-Path $env:windir 'System32\notepad.exe') -ArgumentList ('"' + $log + '"') -WindowStyle Normal
        }
    }
} catch {
    if ($started) { Add-Content -LiteralPath $status -Value ('launcher_error=' + $_.Exception.Message) }
    Write-Error -ErrorAction Continue $_
} finally {
    try {
        if ($started) {
            Add-Content -LiteralPath $status -Value "ended_utc=$([DateTime]::UtcNow.ToString('o'))"
            Write-Host ''
            Write-Host "Diagnostic status: $status"
        }
    } finally {
        $env:VICTORY_SKIP_PIPELINES = $previousSkip
        $env:VICTORY_PIPELINE_WAIT_MS = $previousWait
        $env:VICTORY_SCENE_PREVIEW = $previousPreview
        $runLock.Dispose()
    }
}




