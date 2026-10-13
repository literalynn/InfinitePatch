<#
  InfinitePatch - modern PC fixes for BioShock Infinite
  https://github.com/literalynn/InfinitePatch

  Applies the options from InfinitePatch.ini to the game's config files
  (Documents\My Games\BioShock Infinite\XGame\Config) and to the Windows
  per-game graphics settings. Game files are never modified. The native fixes
  live in Binaries\Win32\dinput8.dll, which reads InfinitePatch.ini itself.

  InfinitePatch.ps1             apply InfinitePatch.ini (run again after editing it)
  InfinitePatch.ps1 -Uninstall  restore the settings saved on the first install
#>
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
trap { Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red; exit 1 }

$game = $PSScriptRoot
$exe = Join-Path $game 'Binaries\Win32\BioShockInfinite.exe'
$docs = if (Test-Path (Join-Path $game '.egstore')) { 'BioShock Infinite Epic' } else { 'BioShock Infinite' }
$xgame = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\$docs\XGame"
$cfgDir = Join-Path $xgame 'Config'
$backup = Join-Path $xgame 'InfinitePatch-backup'
$files = 'XEngine.ini', 'XUserOptions.ini', 'XUI.ini'
$regKey = 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences'
$dll = Join-Path $game 'Binaries\Win32\dinput8.dll'
$dllIsOurs = (Test-Path $dll) -and (Get-Item $dll).VersionInfo.ProductName -eq 'InfinitePatch'

if (-not (Test-Path $exe)) { throw "BioShockInfinite.exe not found. Extract InfinitePatch into the game folder (the one containing 'Binaries' and 'XGame')." }
if (Get-Process BioShockInfinite -ErrorAction SilentlyContinue) { throw 'Close the game first.' }

if ($Uninstall) {
    if (-not (Test-Path $backup) -and -not $dllIsOurs) { throw 'InfinitePatch is not installed, nothing to uninstall.' }
    if ($dllIsOurs) { Remove-Item $dll }
    if (Test-Path $backup) {
        foreach ($f in $files) { Copy-Item (Join-Path $backup $f) $cfgDir -Force }
        $old = Get-Content (Join-Path $backup 'GpuPreferences.txt') -Raw
        if ($old -and $old.Trim()) { Set-ItemProperty $regKey -Name $exe -Value $old.Trim() }
        else { Remove-ItemProperty $regKey -Name $exe -ErrorAction SilentlyContinue }
        Remove-Item $backup -Recurse
    }
    Write-Host 'InfinitePatch uninstalled. Your original settings are restored.' -ForegroundColor Green
    exit 0
}

if (-not (Test-Path (Join-Path $cfgDir 'XEngine.ini'))) {
    throw "Game settings not found in $cfgDir. Launch the game once, close it, then run Install.bat again."
}

# --- read InfinitePatch.ini -------------------------------------------------
$opt = @{}
foreach ($line in Get-Content (Join-Path $game 'InfinitePatch.ini')) {
    if ($line -match '^\s*(\w+)\s*=\s*([^;\s]*)') { $opt[$Matches[1]] = $Matches[2] }
}
$inv = [Globalization.CultureInfo]::InvariantCulture
function Get-Opt($name, [double]$min, [double]$max) {
    $v = 0.0
    $ok = $opt.ContainsKey($name) -and [double]::TryParse($opt[$name], [Globalization.NumberStyles]::Float, $inv, [ref]$v)
    if (-not $ok -or $v -lt $min -or $v -gt $max) { throw "InfinitePatch.ini: $name must be a number from $min to $max." }
    $v
}
function Get-Bool($name) { if ((Get-Opt $name 0 1) -eq 1) { 'True' } else { 'False' } }

$displayMode = [int](Get-Opt DisplayMode 0 2)
$resX = [int](Get-Opt ResolutionX 0 16384)
$resY = [int](Get-Opt ResolutionY 0 16384)
if ($resX -eq 0 -or $resY -eq 0) {
    # DPI aware first, otherwise Windows reports a scaled-down resolution when display scaling is above 100%
    Add-Type -Namespace Win32 -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
    [void][Win32.Dpi]::SetProcessDPIAware()
    Add-Type -AssemblyName System.Windows.Forms
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $resX = $screen.Width; $resY = $screen.Height
}
$vsync = Get-Bool VSync
$maxFps = [int](Get-Opt MaxFPS 0 1000)
$uiScale = (Get-Opt UIScale 0 1).ToString('0.0#####', $inv)
$fovRange = [int](Get-Opt FOVSliderRange 0 100)
$shadowRes = [int](Get-Opt ShadowResolution 0 8192)
if ($shadowRes -notin 0, 1024, 2048, 4096, 8192) { throw 'InfinitePatch.ini: ShadowResolution must be 0, 1024, 2048, 4096 or 8192.' }

# --- edit the game's config files -------------------------------------------
function Set-Keys($text, $file, [hashtable]$keys) {
    foreach ($k in $keys.Keys) {
        $re = "(?m)^$k=[^\r\n]*"
        if ($text -notmatch $re) { throw "$k not found in $file. Delete $cfgDir, launch the game once, then try again." }
        $text = $text -replace $re, "$k=$($keys[$k])"
    }
    $text
}
$read = @{}
foreach ($f in $files) { $read[$f] = [IO.File]::ReadAllText((Join-Path $cfgDir $f)) }

$engine = @{
    DisplayMode = $displayMode; ResX = $resX; ResY = $resY; UseVsync = $vsync
    AllowJoystickInput = [int](1 - (Get-Opt DisableControllers 0 1))
    bSmoothFrameRate = if ($maxFps -gt 0) { 'TRUE' } else { 'FALSE' }
}
if ($maxFps -gt 0) { $engine.MaxSmoothedFrameRate = $maxFps }
$graphics = @{ LensFlares = 'LensFlares'; DepthOfField = 'DepthOfField'; MotionBlur = 'MotionBlur'; HighQualityLightShafts = 'UseHighQualityLightShaftRendering' }
foreach ($o in $graphics.Keys) {
    $v = Get-Opt $o -1 1
    if ($v -ge 0) { $engine[$graphics[$o]] = Get-Bool $o }
}
if ($shadowRes -gt 0) {
    foreach ($k in 'MinShadowResolution', 'MinPreShadowResolution', 'MaxShadowResolution', 'MaxPerObjectShadowResolution') { $engine[$k] = $shadowRes }
}
$xengine = Set-Keys $read['XEngine.ini'] 'XEngine.ini' $engine

# intro videos: a skipped one is taken out of the startup list (the game drops commented lines
# when it saves anyway), a wanted one goes back before LoopingMovieUntilStartupFinishes, which
# must stay or the menu never loads
$loading = '(?m)^(?=StartupMovies=LoopingMovieUntilStartupFinishes\r?$)'
if ($xengine -notmatch $loading) { throw "StartupMovies=LoopingMovieUntilStartupFinishes not found in XEngine.ini. Delete $cfgDir, launch the game once, then try again." }
$intros = [ordered]@{ SkipIntro2K = '2KLogoSweep720p2997'; SkipIntroIrrational = 'IrrationalLogoTest'; SkipIntroUnreal = 'UE3AnimatedLogo_ProRes'; SkipIntroAMD = 'AMDLogo720p' }
foreach ($o in $intros.Keys) {
    $line = "StartupMovies=$($intros[$o])"
    $xengine = $xengine -replace "(?m)^;?$([regex]::Escape($line))\r?\n", ''
    if ((Get-Opt $o 0 1) -eq 0) { $xengine = $xengine -replace $loading, "$line`r`n" }
}

# the game re-applies the Default* values until its options have been saved once
$xuser = Set-Keys $read['XUserOptions.ini'] 'XUserOptions.ini' @{
    DisplayMode = $displayMode; DefaultDisplayMode = $displayMode
    ResolutionX = $resX; ResolutionY = $resY; DefaultResolutionX = $resX; DefaultResolutionY = $resY
    bLockFramerate = $vsync; DefaultbLockFramerate = $vsync
    MaxUserFOVOffsetPercent = $fovRange
}
$xui = Set-Keys $read['XUI.ini'] 'XUI.ini' @{ MaxResolutionUpScale = $uiScale }

# read by dinput8.dll at game start, validated here so a typo does not go unnoticed
$native = @{ FixHighPollingRate = Get-Bool FixHighPollingRate; FixSprintSensitivity = Get-Bool FixSprintSensitivity }

$gpu = ''
if ((Get-Opt WindowedOptimizations 0 1) -eq 1) { $gpu += 'SwapEffectUpgradeEnable=1;' }
if ((Get-Opt ForceDedicatedGPU 0 1) -eq 1) { $gpu += 'GpuPreference=2;' }

# --- everything validated: back up once, then write -------------------------
if (-not (Test-Path $backup)) {
    New-Item $backup -ItemType Directory | Out-Null
    foreach ($f in $files) { Copy-Item (Join-Path $cfgDir $f) $backup }
    $prev = (Get-ItemProperty $regKey -ErrorAction SilentlyContinue).$exe
    Set-Content (Join-Path $backup 'GpuPreferences.txt') "$prev"
}
[IO.File]::WriteAllText((Join-Path $cfgDir 'XEngine.ini'), $xengine)
[IO.File]::WriteAllText((Join-Path $cfgDir 'XUserOptions.ini'), $xuser)
[IO.File]::WriteAllText((Join-Path $cfgDir 'XUI.ini'), $xui)

if (-not (Test-Path $regKey)) { New-Item $regKey | Out-Null }
if ($gpu) { Set-ItemProperty $regKey -Name $exe -Value $gpu }
else { Remove-ItemProperty $regKey -Name $exe -ErrorAction SilentlyContinue }

$modes = 'Windowed', 'Exclusive fullscreen', 'Borderless fullscreen'
Write-Host 'InfinitePatch applied.' -ForegroundColor Green
Write-Host "  Display : $($modes[$displayMode]), ${resX}x${resY}, VSync $vsync, FPS limit $(if ($maxFps) { $maxFps } else { 'none' })"
if ($dllIsOurs) { Write-Host "  Native  : high polling rate fix $($native.FixHighPollingRate), sprint sensitivity fix $($native.FixSprintSensitivity)" }
elseif (Test-Path $dll) { Write-Host '  Native  : Binaries\Win32\dinput8.dll belongs to another mod, the native fixes are not active' -ForegroundColor Yellow }
else { Write-Host '  Native  : Binaries\Win32\dinput8.dll is missing, extract the whole zip to get the native fixes' -ForegroundColor Yellow }
Write-Host "  Backup  : $backup"
