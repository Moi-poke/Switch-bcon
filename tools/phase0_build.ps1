# phase0_build.ps1 - Phase 0 実験UF2の再現ビルド。
# コミット 68531ce の detached worktree を temp dir に作り、
# IMU_DUMMY=1 で構成・ビルドし、log/switch-bcon-imu-dummy-<Name>.uf2 を出す。
# 出力後、UF2内文字列に git=<hash> と dirty=0 が含まれることを確認する。
# 例: E4軸0正 (GRaw +1475)
#   .\tools\phase0_build.ps1 -Axis 0 -GRaw 1475 -Name e4ax0p
# 例: E2/E3b (yaw軸が2と判明後、90dps、MASK=1)
#   .\tools\phase0_build.ps1 -Axis 2 -GRaw 1475 -Fill 1 -Name e3b-first-90
# 例: E5 (yaw軸が2=Zと判明後、大きさ8192をZから30°傾けてXへ)
#   X=4096, Z=7094 (8192*sin30, 8192*cos30)
#   .\tools\phase0_build.ps1 -AccX 4096 -AccY 0 -AccZ 7094 -Name e5-8192
param(
    [int]$Axis = 2,        # IMU_DUMMY_GYRO_AXIS 0..2
    [int]$GRaw = 5899,     # IMU_DUMMY_G_RAW int16
    [int]$TMs = 4000,      # IMU_DUMMY_T_MS
    [int]$Fill = 7,        # IMU_DUMMY_FILL_MASK bit0=slot0
    [int]$IntervalMs = 8,  # IMU_DUMMY_INTERVAL_MS
    [int]$AccX = 0,        # IMU_DUMMY_ACC_X int16
    [int]$AccY = 0,        # IMU_DUMMY_ACC_Y int16
    [int]$AccZ = 4096,     # IMU_DUMMY_ACC_Z int16
    [int]$Mode = 1,        # IMU_DUMMY_MODE 0=SINE 1=STEP
    [int]$Force = 0,       # IMU_DUMMY_FORCE 0/1
    [int]$Diag = 0,        # IMU_DUMMY_DIAG (§1: 0/1)
    [string]$Commit = "",  # ビルド元コミット (空=既定 §1COMMIT)
    [Parameter(Mandatory = $true)][string]$Name  # 出力名 (英数・-・_ のみ)
)
$ErrorActionPreference = "Stop"

if ($Commit -eq "") { $Commit = "§1COMMIT" }
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw "Name must be [A-Za-z0-9_-]+: $Name" }
if ($Axis -lt 0 -or $Axis -gt 2) { throw "Axis must be 0..2" }
if ($GRaw -lt -32768 -or $GRaw -gt 32767) { throw "GRaw must fit int16" }
if ($Fill -lt 0 -or $Fill -gt 7) { throw "Fill must be 0..7" }
if ($AccX -lt -32768 -or $AccX -gt 32767) { throw "AccX must fit int16" }
if ($AccY -lt -32768 -or $AccY -gt 32767) { throw "AccY must fit int16" }
if ($AccZ -lt -32768 -or $AccZ -gt 32767) { throw "AccZ must fit int16" }
if ($Mode -ne 0 -and $Mode -ne 1) { throw "Mode must be 0 or 1" }
if ($Force -ne 0 -and $Force -ne 1) { throw "Force must be 0 or 1" }
if ($Diag -ne 0 -and $Diag -ne 1) { throw "Diag must be 0 or 1" }
if ($Commit -notmatch '^[A-Za-z0-9]+$') { throw "Commit must be hex: $Commit" }

$Repo = Split-Path (Split-Path $MyInvocation.MyCommand.Path -Parent) -Parent
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
foreach ($t in @($cmake, $ninja)) { if (-not (Test-Path $t)) { throw "missing tool: $t" } }

# arm-none-eabi-strings の探索 (UF2内文字列の確認用)。
$strings = Get-ChildItem "$env:USERPROFILE/.pico-sdk/toolchain/*/bin/arm-none-eabi-strings.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $strings) { throw "arm-none-eabi-strings.exe not found" }

$Wt = Join-Path ([IO.Path]::GetTempPath()) "phase0wt-$Name"
# ネイティブ呼出し用: $ErrorActionPreference=Stop の下ではstderr出力が
# 例外化されるため、一時的にContinueへ緩めて終了コードで判定する。
function Invoke-Native {
    param([scriptblock]$Cmd, [string]$What)
    $eap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Cmd 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
    } finally {
        $ErrorActionPreference = $eap
    }
}
try { Invoke-Native { & git -C $Repo worktree remove --force $Wt } "pre-clean" } catch { }
Invoke-Native { & git -C $Repo worktree add --detach $Wt $Commit } "git worktree add"
try {
    $Bd = Join-Path $Wt "build"
    Invoke-Native { & $cmake -S $Wt -B $Bd -G Ninja "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja" `
        -DIMU_DUMMY=1 "-DIMU_DUMMY_GYRO_AXIS=$Axis" "-DIMU_DUMMY_G_RAW=$GRaw" `
        "-DIMU_DUMMY_T_MS=$TMs" "-DIMU_DUMMY_FILL_MASK=$Fill" `
        "-DIMU_DUMMY_INTERVAL_MS=$IntervalMs" "-DIMU_DUMMY_ACC_X=$AccX" `
        "-DIMU_DUMMY_ACC_Y=$AccY" "-DIMU_DUMMY_ACC_Z=$AccZ" `
        "-DIMU_DUMMY_MODE=$Mode" "-DIMU_DUMMY_FORCE=$Force" "-DIMU_DUMMY_DIAG=$Diag" } "cmake configure"
    Invoke-Native { & $cmake --build $Bd } "cmake build"
    $Uf2 = Join-Path $Bd "switch-bcon.uf2"
    if (-not (Test-Path $Uf2)) { throw "UF2 not produced" }
    $Out = Join-Path $Repo "log/switch-bcon-imu-dummy-$Name.uf2"
    Copy-Item $Uf2 $Out -Force
    # 由来確認。起動行は "git=%s dirty=%s" 形式で値と分離されるため、
    # UF2内では3点で確認する: ①hash値 ②dirty形式 ③worktree自体がclean
    # (③が空ならCMakeはdirty="0"を埋め込んでいる)。
    $str = & $strings $Out
    if (-not ($str -match [regex]::Escape($Commit))) { throw "UF2 lacks git value $Commit" }
    if (-not ($str -match 'dirty=')) { throw "UF2 lacks dirty format" }
    $eap2 = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    try { $st = & git -C $Wt status --porcelain 2>&1 } finally { $ErrorActionPreference = $eap2 }
    if ($st) { throw "worktree not clean: $st" }
    Write-Host "OK: $Out"
} finally {
    try { & git -C $Repo worktree remove --force $Wt 2>&1 | Out-Null } catch { }
    try { & git -C $Repo worktree prune 2>&1 | Out-Null } catch { }
}
