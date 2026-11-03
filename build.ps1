# CE6ML 构建脚本（纯 g++ 版，无需 CMake）
# 用法:
#   .\build.ps1                # Release 构建
#   .\build.ps1 -Config Debug  # Debug 构建
#   .\build.ps1 -Clean         # 清理后重新构建
#
# 依赖: git、MinGW g++（在 PATH 中）。首次运行会自动克隆 safetyhook / zydis 到 deps\
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$DepsDir    = Join-Path $ProjectDir "deps"
$BuildDir   = Join-Path $ProjectDir "build"
$ObjDir     = Join-Path $BuildDir "obj"

# --- 检查工具 ---
foreach ($tool in @("git", "gcc", "g++")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "未找到 $tool，请安装 MinGW（把 bin 加入 PATH）和 git"
        exit 1
    }
}
Write-Host "编译器: $((g++ --version)[0])" -ForegroundColor Cyan

# --- 克隆依赖（带镜像回退，直连失败时自动换源）---
function Git-Clone-Fallback {
    param(
        [string]$Repo,      # 例如 zyantific/zydis
        [string]$Branch,    # 分支或 tag
        [string]$Dest,
        [switch]$Submodules
    )
    if (Test-Path $Dest) { return }
    $urls = @(
        "https://github.com/$Repo.git",
        "https://gh-proxy.com/https://github.com/$Repo.git",
        "https://gitclone.com/github.com/$Repo.git"
    )
    $subArgs = if ($Submodules) { @("--recurse-submodules", "--shallow-submodules") } else { @() }
    foreach ($u in $urls) {
        Write-Host "克隆 $Repo （源: $u）"
        git clone --depth 1 --branch $Branch @subArgs $u $Dest
        if ($LASTEXITCODE -eq 0) { return }
        if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
        Write-Host "源不可用，尝试下一个..." -ForegroundColor Yellow
    }
    Write-Error "克隆 $Repo 失败（所有源均不可用），请检查网络/代理后重试"
    exit 1
}

Git-Clone-Fallback -Repo "zyantific/zydis" -Branch "v4.1.0" -Dest (Join-Path $DepsDir "zydis") -Submodules
Git-Clone-Fallback -Repo "cursey/safetyhook" -Branch "main" -Dest (Join-Path $DepsDir "safetyhook")

# --- 清理 ---
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "清理 $BuildDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force $ObjDir | Out-Null

# --- 源文件清单 ---
$zydisDir  = Join-Path $DepsDir "zydis"
$zycoreDir = Join-Path $zydisDir "dependencies\zycore"
$shDir     = Join-Path $DepsDir "safetyhook"

$cSources = @()
$cSources += Get-ChildItem -Recurse -Filter *.c (Join-Path $zycoreDir "src") | ForEach-Object FullName
$cSources += Get-ChildItem -Recurse -Filter *.c (Join-Path $zydisDir "src")  | ForEach-Object FullName

# safetyhook: 只用 InlineHook，排除 easy/mid_hook(需要 masm 汇编)/os.linux
$cppSources = @("allocator.cpp", "inline_hook.cpp", "os.windows.cpp", "utility.cpp", "vmt_hook.cpp") |
    ForEach-Object { Join-Path $shDir "src\$_" }
$cppSources += Join-Path $ProjectDir "src\dllmain.cpp"

# --- 编译参数 ---
$optFlags = if ($Config -eq "Release") { @("-O2", "-DNDEBUG") } else { @("-O0", "-g") }
$defines  = @("-DZYDIS_STATIC_BUILD", "-DZYCORE_STATIC_BUILD")
$includes = @(
    "-I$(Join-Path $zydisDir 'include')",
    "-I$(Join-Path $zydisDir 'src')",
    "-I$(Join-Path $zycoreDir 'include')",
    "-I$(Join-Path $shDir 'include')",
    "-I$(Join-Path $ProjectDir 'src')",
    "-I$ProjectDir"
)

# --- 编译（跳过未改动的 .o）---
# 注：MinGW 链接器不认中文路径，所以 .o 文件名用 ASCII（文件名 + 路径哈希防重名），
#     链接时统一切换到项目目录使用相对路径
$md5 = [System.Security.Cryptography.MD5]::Create()
$objects = @()
$compileJobs = @()
foreach ($f in $cSources)  { $compileJobs += ,@("gcc", @(), $f) }
foreach ($f in $cppSources) { $compileJobs += ,@("g++", @("-std=c++23"), $f) }

$needCompile = 0
# 项目头文件改动时触发重编译（依赖库头文件视为不变）
$srcHeaders = Get-ChildItem -Filter *.hpp (Join-Path $ProjectDir "src") | ForEach-Object FullName
foreach ($job in $compileJobs) {
    $compiler, $stdFlags, $src = $job
    $hash = [System.BitConverter]::ToString($md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($src))).Replace('-', '').Substring(0, 8).ToLower()
    $objName = "{0}_{1}.o" -f [IO.Path]::GetFileNameWithoutExtension($src), $hash
    $obj = Join-Path $ObjDir $objName
    $objects += $obj

    if (Test-Path $obj) {
        $objTime = (Get-Item $obj).LastWriteTime
        $srcTime = (Get-Item $src).LastWriteTime
        $hdrNewer = ($src -like "$(Join-Path $ProjectDir 'src')*") -and ($srcHeaders | Where-Object { (Get-Item $_).LastWriteTime -gt $objTime } | Select-Object -First 1)
        if ($objTime -ge $srcTime -and -not $hdrNewer) {
            continue
        }
    }
    $needCompile++
    Write-Host "编译 $($src | Split-Path -Leaf)"
    & $compiler @stdFlags @optFlags @defines @includes -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { Write-Error "编译失败: $src"; exit 1 }
}
if ($needCompile -eq 0) { Write-Host "所有源文件均为最新" -ForegroundColor Cyan }

# --- 链接（相对路径，规避 MinGW ld 的中文路径问题）---
$out = Join-Path $BuildDir "xinput1_3.dll"
Write-Host "`n链接 $out"
Push-Location $ProjectDir
try {
    $rsp = Join-Path $ObjDir "link.rsp"
    $objects | ForEach-Object { (Resolve-Path -Relative $_) -replace '\\', '/' } | Set-Content -Encoding ascii $rsp
    $rspRel = (Resolve-Path -Relative $rsp) -replace '\\', '/'
    g++ -shared -static -o "build/xinput1_3.dll" "@$rspRel" "xinput.def" -lntdll -lstdc++exp
    if ($LASTEXITCODE -ne 0) { Write-Error "链接失败"; exit 1 }
}
finally {
    Pop-Location
}

Write-Host "`n构建成功: $out ($Config)" -ForegroundColor Green
Write-Host "将 xinput1_3.dll 复制到游戏目录即可使用"
