<#
.SYNOPSIS
One-step build of Excitebots: Trick Racing PC on Windows, from your own disc image.

.DESCRIPTION
Run from PowerShell in the repository folder:

    powershell -ExecutionPolicy Bypass -File scripts\excitebots\build-windows.ps1 "D:\Games\Excitebots (USA).wbfs"

Accepts a .wbfs/.iso/.rvz/.ciso/.wia disc image, a .zip holding one (or the first part,
.zip.001, of a zip split by 7-Zip), or a folder already extracted with nodtool (the one holding
sys\ and files\). The result is a self-contained folder, Excitebots\ in the repository, with
Excitebots.exe and its settings (Excitebots\UserData).

From a repository checkout this needs Git for Windows (winget install Git.Git). Players don't run
this by hand: the Excite PC launcher runs it from setup\bots\ with a payload folder beside it,
shows its "##progress" lines as a progress bar, and needs nothing installed. Everything else
(compiler, CMake, Ninja, .NET, nodtool, libraries) is downloaded into the work folder with pinned
checksums. About 20 GB of free space during the build; the finished game is much smaller.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)] [string]$Disc,
    # Where the game goes. Default: <repo>\Excitebots
    [string]$Out,
    # Scratch space for tools, sources and the build. Default: C:\EBBuild (a short path
    # avoids Windows' path-length limit; it can be deleted after the build).
    [string]$Work,
    # Parallel compile jobs. Default: one per core, capped by memory.
    [int]$Jobs = 0,
    # Put an "Excitebots" shortcut on the desktop.
    [switch]$Shortcut
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$WiiCompiledUrl = 'https://github.com/patchzyy/Wiicompiled.git'
$WiiCompiledCommit = '4993b2226c8f24defe84fadf04e69732c635a90d'   # v0.9.2
$AuroraCommit = '3c80a1a9e8d75c61d31a903c6a3cef5f54c02f33'
$NodtoolUrl = 'https://github.com/encounter/nod/releases/download/v1.4.4/nodtool-windows-x86_64.exe'
$NodtoolSha256 = 'fb3203a68a59fa19ba9de0aa2f8c339396b6e4c5f9274d0858d302074cdedf8b'
$DolSha256 = '14ed09f70c8f9fcd5c027bb9e423ba2deade3c15bbd7d4376b2f63fe99c80c1e'

$Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
# Setup mode: the launcher's setup\bots folder ships the patched files (payload\overlay) instead of a checkout.
$Payload = Join-Path $PSScriptRoot 'payload'
$SetupMode = Test-Path -LiteralPath (Join-Path $Payload 'overlay') -PathType Container
# Shaders recorded from play, all compiled on the startup screen so races never stutter.
$SeedCache = if ($SetupMode) { Join-Path $Payload 'initial_pipeline_cache.db' } else { Join-Path $Repo 'assets\shaders\excitebots\initial_pipeline_cache.db' }
if (-not $Out) { $Out = Join-Path $Repo 'Excitebots' }
# Its own work folder: Excite Truck's (C:\ETBuild) holds a differently patched WiiCompiled.
if (-not $Work) { $Work = Join-Path $env:SystemDrive 'EBBuild' }
$Out = [IO.Path]::GetFullPath($Out)
$Work = [IO.Path]::GetFullPath($Work)

# Keep a log of every run next to the build files, so a failure can be read after the window closes.
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Log = Join-Path $Work 'build-log.txt'
try { Start-Transcript -LiteralPath $Log -Append | Out-Null } catch { }
Write-Host "Log: $Log"
trap {
    Write-Host ''
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host ($_.ScriptStackTrace)
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}

# A step's title, and where the launcher's progress bar stands when it starts. "##progress A B"
# means the steps after it run from A to B percent, moved along by ninja's [n/m] lines.
function Step([string]$Text, [string]$Progress = '') {
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
    if ($Progress) { Write-Host "##progress $Progress" }
}
function Fail([string]$Text) {
    Write-Host ''; Write-Host "Error: $Text" -ForegroundColor Red
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
function Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Run([string]$Exe, [string[]]$Arguments) {
    # Pipe through Out-Host so the tool's output also lands in the log. Errors on stderr are
    # just text here; the exit code decides.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { "$_" } | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "$([IO.Path]::GetFileName($Exe)) $($Arguments[0]) failed (exit code $LASTEXITCODE)" }
}
# Builds an .ico from the Wii save icon inside mcbanner.res (a WIBN banner: the 48x48 RGB5A3
# icon sits 0x60A0 bytes in). Sizes 16 to 256 are stored as PNG, which Windows Vista and later read.
function Write-EtIcon([string]$BannerRes, [string]$IcoPath) {
    Add-Type -AssemblyName System.Drawing
    $bytes = [IO.File]::ReadAllBytes($BannerRes)
    $wibn = -1
    for ($i = 0; $i -lt $bytes.Length - 4; $i += 4) {
        if ($bytes[$i] -eq 0x57 -and $bytes[$i + 1] -eq 0x49 -and $bytes[$i + 2] -eq 0x42 -and $bytes[$i + 3] -eq 0x4E) { $wibn = $i; break }
    }
    if ($wibn -lt 0) { throw "no save banner in $BannerRes" }
    $base = $wibn + 0x60A0
    $icon = New-Object System.Drawing.Bitmap 48, 48, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $k = 0
    for ($ty = 0; $ty -lt 48; $ty += 4) {
        for ($tx = 0; $tx -lt 48; $tx += 4) {
            for ($y = 0; $y -lt 4; $y++) {
                for ($x = 0; $x -lt 4; $x++) {
                    $v = ([int]$bytes[$base + $k] -shl 8) -bor $bytes[$base + $k + 1]; $k += 2
                    if ($v -band 0x8000) {
                        $a = 255; $r = (($v -shr 10) -band 31) * 255 / 31; $g = (($v -shr 5) -band 31) * 255 / 31; $b = ($v -band 31) * 255 / 31
                    } else {
                        $a = (($v -shr 12) -band 7) * 255 / 7; $r = (($v -shr 8) -band 15) * 17; $g = (($v -shr 4) -band 15) * 17; $b = ($v -band 15) * 17
                    }
                    $icon.SetPixel($tx + $x, $ty + $y, [System.Drawing.Color]::FromArgb([int]$a, [int]$r, [int]$g, [int]$b))
                }
            }
        }
    }
    $images = foreach ($size in 16, 32, 48, 256) {
        $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $gfx.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $gfx.DrawImage($icon, 0, 0, $size, $size)
        $gfx.Dispose()
        $png = New-Object IO.MemoryStream
        $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        [pscustomobject]@{ Size = $size; Data = $png.ToArray() }
    }
    $icon.Dispose()
    $out = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter $out
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($img in $images) {
        $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
        $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$img.Data.Length); $w.Write([uint32]$offset)
        $offset += $img.Data.Length
    }
    foreach ($img in $images) { $w.Write($img.Data) }
    $w.Flush()
    [IO.File]::WriteAllBytes($IcoPath, $out.ToArray())
}
function Download([string]$Uri, [string]$Path, [string]$Sha) {
    if ((Test-Path -LiteralPath $Path -PathType Leaf) -and (Sha256 $Path) -eq $Sha) { return }
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile "$Path.partial"
    if ((Sha256 "$Path.partial") -ne $Sha) { Remove-Item -LiteralPath "$Path.partial" -Force; Fail "download is corrupt: $Uri" }
    Move-Item -LiteralPath "$Path.partial" -Destination $Path -Force
}

if (-not (Test-Path -LiteralPath $Disc)) { Fail "not found: $Disc" }
$Disc = (Resolve-Path -LiteralPath $Disc).Path
if ($Work.Contains(' ')) { Fail "the work folder path must not contain spaces: $Work (pass -Work C:\EBBuild)" }
[IO.Directory]::CreateDirectory($Work) | Out-Null
[IO.Directory]::CreateDirectory($Out) | Out-Null
$downloads = Join-Path $Work 'downloads'
[IO.Directory]::CreateDirectory($downloads) | Out-Null

Step 'Checking tools' '1'
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $SetupMode -and -not $git) { Fail 'Git is not installed. Install it with: winget install Git.Git   (then open a new PowerShell window)' }
if (-not (Test-Path -LiteralPath (Join-Path $env:SystemRoot 'System32\vcruntime140.dll'))) {
    Fail 'The Microsoft Visual C++ runtime is missing. Install it with: winget install Microsoft.VCRedist.2015+.x64'
}
if ($Jobs -le 0) {
    $memoryGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    # Translated sources are large; allow roughly 2 GB per compile job.
    $Jobs = [math]::Max(1, [math]::Min([Environment]::ProcessorCount, [math]::Floor($memoryGb / 2)))
}

# .NET 8 SDK for the translator: use an installed one, or put a private copy in the work folder.
$dotnet = $null
$installed = Get-Command dotnet -ErrorAction SilentlyContinue
if ($installed) {
    $sdks = & $installed.Source --list-sdks 2>$null
    if ($sdks | Where-Object { $_ -match '^(\d+)\.' -and [int]$Matches[1] -ge 8 }) { $dotnet = $installed.Source }
}
if (-not $dotnet) {
    $dotnetRoot = Join-Path $Work 'dotnet'
    $dotnet = Join-Path $dotnetRoot 'dotnet.exe'
    if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
        Step 'Downloading the .NET 8 SDK (private copy, no admin needed)' '2'
        $installer = Join-Path $downloads 'dotnet-install.ps1'
        Invoke-WebRequest -UseBasicParsing -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer
        & $installer -Channel 8.0 -InstallDir $dotnetRoot -NoPath
        if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) { Fail '.NET SDK install failed' }
    }
}
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'

# --- Disc --------------------------------------------------------------------------------
$data = Join-Path $Out 'Disc'
if (Test-Path -LiteralPath (Join-Path $Disc 'sys\main.dol') -PathType Leaf) {
    $data = $Disc
} elseif (-not (Test-Path -LiteralPath (Join-Path $data 'sys\main.dol') -PathType Leaf)) {
    $image = $Disc
    if ($Disc -match '\.zip\.\d{3}$') {
        # A zip split into parts by 7-Zip (name.zip.001, .002, ...): join the parts back into one zip.
        Step 'Joining the zip parts' '4'
        $prefix = $Disc.Substring(0, $Disc.Length - 4)
        $joined = Join-Path $Work 'disc.zip'
        $output = [IO.File]::Create($joined)
        try {
            for ($part = 1; Test-Path -LiteralPath ('{0}.{1:D3}' -f $prefix, $part); $part++) {
                $stream = [IO.File]::OpenRead(('{0}.{1:D3}' -f $prefix, $part))
                try { $stream.CopyTo($output) } finally { $stream.Dispose() }
            }
        } finally { $output.Dispose() }
        $Disc = $joined
        $image = $joined
    }
    if ([IO.Path]::GetExtension($image) -ieq '.zip') {
        Step 'Unpacking the zip' '5'
        $unzipped = Join-Path $Work 'disc-zip'
        if (Test-Path -LiteralPath $unzipped) { Remove-Item -LiteralPath $unzipped -Recurse -Force }
        Expand-Archive -LiteralPath $image -DestinationPath $unzipped
        $image = Get-ChildItem -LiteralPath $unzipped -Recurse -File |
            Where-Object { $_.Extension -match '^\.(wbfs|iso|rvz|ciso|wia|gcz)$' } |
            Select-Object -First 1 -ExpandProperty FullName
        if (-not $image) { Fail 'the zip holds no .wbfs/.iso/.rvz disc image' }
    }
    $nodtool = Join-Path $Work 'tools\nodtool.exe'
    [IO.Directory]::CreateDirectory((Split-Path -Parent $nodtool)) | Out-Null
    Step 'Downloading nodtool (reads Wii disc images)' '7'
    Download $NodtoolUrl $nodtool $NodtoolSha256
    Step 'Extracting the disc' '8'
    $partial = "$data.partial"
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Recurse -Force }
    Run $nodtool @('extract', $image, $partial)
    Move-Item -LiteralPath $partial -Destination $data
    foreach ($temporary in (Join-Path $Work 'disc-zip'), (Join-Path $Work 'disc.zip')) {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
    }
}
$dol = Join-Path $data 'sys\main.dol'
if (-not (Test-Path -LiteralPath $dol -PathType Leaf)) { Fail "no sys\main.dol in $data" }
if ((Sha256 $dol) -ne $DolSha256) {
    Fail "this is not the US Excitebots disc (RX3E01); other versions aren't supported yet"
}

# --- WiiCompiled, patched for Excite Truck and then Excitebots ----------------------------
$ws = Join-Path $Work 'wiicompiled'
$stampFile = Join-Path $ws '.eb-patched'
$current = if (Test-Path -LiteralPath $stampFile) { [IO.File]::ReadAllText($stampFile) } else { '' }
if ($SetupMode) {
    $stamp = [IO.File]::ReadAllText((Join-Path $Payload 'stamp.txt')).Trim()
    if ($current -ne $stamp) {
        Step 'Downloading WiiCompiled (the recompiler and runtime this port is built on)' '12'
        [IO.Directory]::CreateDirectory($ws) | Out-Null
        # Fresh sources; keep the build, translation output and downloaded build tools.
        Get-ChildItem -LiteralPath $ws -Force |
            Where-Object { $_.Name -notin 'build', 'generated', 'Assets', 'Launcher' } |
            Remove-Item -Recurse -Force
        $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
        $sources = @(
            @{ Uri = "https://github.com/patchzyy/Wiicompiled/archive/$WiiCompiledCommit.zip"; Target = $ws },
            @{ Uri = "https://github.com/patchzyy/aurora-wiicompiled/archive/$AuroraCommit.zip"; Target = (Join-Path $ws 'aurora-main') }
        )
        foreach ($source in $sources) {
            $archive = Join-Path $downloads ([IO.Path]::GetFileName($source.Uri))
            if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
                Invoke-WebRequest -UseBasicParsing -Uri $source.Uri -OutFile "$archive.partial"
                Move-Item -LiteralPath "$archive.partial" -Destination $archive -Force
            }
            $extract = Join-Path $Work 'src-extract'
            if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
            [IO.Directory]::CreateDirectory($extract) | Out-Null
            & $tar -xf $archive -C $extract
            if ($LASTEXITCODE -ne 0) { Fail "could not unpack $archive" }
            $root = Get-ChildItem -LiteralPath $extract -Directory | Select-Object -First 1
            & robocopy $root.FullName $source.Target /E /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -ge 8) { Fail "could not copy $($root.Name)" }
            Remove-Item -LiteralPath $extract -Recurse -Force
        }
        Step 'Applying the Excitebots changes' '15'
        & robocopy (Join-Path $Payload 'overlay') $ws /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Fail 'could not copy the Excitebots changes' }
        [IO.File]::WriteAllText($stampFile, $stamp)
    }
} else {
$stampInputs = @(Get-ChildItem -LiteralPath (Join-Path $Repo 'patches') -Filter '*.patch' -File) +
    @(Get-ChildItem -LiteralPath (Join-Path $Repo 'patches\excitebots') -Filter '*.patch' -File) +
    @(Get-Item -LiteralPath (Join-Path $Repo 'tools\sdk_map\et_versions.inc')) +
    @(Get-Item -LiteralPath (Join-Path $Repo 'tools\sdk_map\eb_versions.inc')) +
    @(Get-ChildItem -LiteralPath (Join-Path $Repo 'projects\excitebots') -File)
$stamp = ($stampInputs | Sort-Object FullName | ForEach-Object { Sha256 $_.FullName }) -join ','
if ($current -ne $stamp) {
    Step 'Fetching WiiCompiled (the recompiler and runtime this port is built on)' '12'
    if (-not (Test-Path -LiteralPath (Join-Path $ws '.git'))) {
        if (Test-Path -LiteralPath $ws) { Remove-Item -LiteralPath $ws -Recurse -Force }
        # Keep LF line endings so the patches apply.
        Run $git.Source @('-c', 'core.autocrlf=false', 'clone', '--quiet', $WiiCompiledUrl, $ws)
        Run $git.Source @('-C', $ws, 'config', 'core.autocrlf', 'false')
    }
    & $git.Source -C $ws fetch --quiet origin $WiiCompiledCommit 2>$null
    Run $git.Source @('-C', $ws, 'checkout', '--quiet', '--force', $WiiCompiledCommit)
    Run $git.Source @('-C', $ws, 'clean', '-fdq', '-e', 'build', '-e', 'generated', '-e', 'Assets', '-e', 'Launcher/artifacts')
    Run $git.Source @('-C', $ws, '-c', 'core.autocrlf=false', 'submodule', 'update', '--init', '--quiet', 'aurora-main')
    $aurora = Join-Path $ws 'aurora-main'
    Run $git.Source @('-C', $aurora, 'config', 'core.autocrlf', 'false')
    Run $git.Source @('-C', $aurora, 'checkout', '--quiet', '--force', $AuroraCommit)
    Run $git.Source @('-C', $aurora, 'clean', '-fdq')

    Step 'Applying the Excite Truck and Excitebots patches' '15'
    Run $git.Source @('-C', $ws, 'apply', '--whitespace=nowarn', (Join-Path $Repo 'patches\0001-excite-truck-runtime.patch'))
    Run $git.Source @('-C', $aurora, 'apply', '--whitespace=nowarn', (Join-Path $Repo 'patches\0002-aurora-channel-colors.patch'))
    Run $git.Source @('-C', $ws, 'apply', '--whitespace=nowarn', (Join-Path $Repo 'patches\0003-xbox-button-prompts.patch'))
    foreach ($patch in Get-ChildItem -LiteralPath (Join-Path $Repo 'patches\excitebots') -Filter '*.patch' -File | Sort-Object Name) {
        Run $git.Source @('-C', $ws, 'apply', '--whitespace=nowarn', $patch.FullName)
    }
    $versions = Join-Path $ws 'runtime\include\guest_abi\guest_versions.inc'
    [IO.File]::AppendAllText($versions, [IO.File]::ReadAllText((Join-Path $Repo 'tools\sdk_map\et_versions.inc')))
    [IO.File]::AppendAllText($versions, [IO.File]::ReadAllText((Join-Path $Repo 'tools\sdk_map\eb_versions.inc')))
    $project = Join-Path $ws 'projects\excitebots'
    [IO.Directory]::CreateDirectory($project) | Out-Null
    Copy-Item -Path (Join-Path $Repo 'projects\excitebots\*') -Destination $project -Force
    [IO.File]::WriteAllText($stampFile, $stamp)
}
}

# --- Build tools and libraries (WiiCompiled's own pinned set) ----------------------------
$tools = Join-Path $Work 'toolchain'
$deps = Join-Path $Work 'deps'
Step 'Preparing the compiler, CMake and Ninja (downloaded once)' '17'
& (Join-Path $ws 'Launcher\Prepare-PortableTools.ps1') -Destination $tools
$cmake = Join-Path $tools 'CMake\bin\cmake.exe'
Step 'Preparing the libraries (downloaded once)' '23'
# Windows 10's tar.exe can't open .tar.bz2 files, and libusb only ships as one. Unpack it with
# CMake instead and stamp it the way Prepare-Dependencies.ps1 does, so that script skips it.
$libusb = Join-Path $deps 'libusb'
$libusbFile = 'libusb-1.0.30.tar.bz2'
$libusbSha = 'fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf'
$libusbStamp = "$libusbFile $libusbSha"
if (-not ((Test-Path -LiteralPath $libusb -PathType Container) -and (Test-Path -LiteralPath "$libusb.source" -PathType Leaf) -and
          ([IO.File]::ReadAllText("$libusb.source") -eq $libusbStamp))) {
    $downloadDir = Join-Path $ws 'Launcher\artifacts\downloads'
    [IO.Directory]::CreateDirectory($downloadDir) | Out-Null
    $archive = Join-Path $downloadDir $libusbFile
    Download "https://github.com/libusb/libusb/releases/download/v1.0.30/$libusbFile" $archive $libusbSha
    [IO.Directory]::CreateDirectory($deps) | Out-Null
    $extract = Join-Path $deps '.extract-libusb'
    if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    [IO.Directory]::CreateDirectory($extract) | Out-Null
    Push-Location $extract
    try { Run $cmake @('-E', 'tar', 'xf', $archive) } finally { Pop-Location }
    $entries = @(Get-ChildItem -LiteralPath $extract -Force)
    $source = if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $entries[0].FullName } else { $extract }
    if (Test-Path -LiteralPath $libusb) { Remove-Item -LiteralPath $libusb -Recurse -Force }
    Move-Item -LiteralPath $source -Destination $libusb
    if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    [IO.File]::WriteAllText("$libusb.source", $libusbStamp)
    Write-Host 'Prepared libusb'
}
& (Join-Path $ws 'Launcher\Prepare-Dependencies.ps1') -Destination $deps
. (Join-Path $ws 'Launcher\NativeBuildFlags.ps1')
$ninja = Join-Path $tools 'Ninja\ninja.exe'
$bin = Join-Path $tools 'llvm-mingw\bin'

# --- Translate the game's code to C++ ----------------------------------------------------
[IO.Directory]::CreateDirectory((Join-Path $ws 'Assets')) | Out-Null
Copy-Item -LiteralPath $dol -Destination (Join-Path $ws 'Assets\main.dol') -Force
$cli = Join-Path $ws 'translator\src\Translator.Cli\bin\Release\net8.0\Translator.Cli.dll'
if (-not (Test-Path -LiteralPath $cli -PathType Leaf)) {
    Step 'Building the translator' '28'
    Run $dotnet @('build', (Join-Path $ws 'translator\src\Translator.Cli\Translator.Cli.csproj'), '-c', 'Release', '-v', 'quiet', '-nologo')
}

Push-Location $ws
$oldPath = $env:PATH
try {
    $p = 'projects/excitebots/recomp.yml'
    Step "Translating the game's code (a few minutes)" '31'
    Run $dotnet @($cli, 'translate-recursive', '0x80004170', '--project', $p, '--outdir', 'generated/functions',
        '--output-metadata', 'generated/base_translation_output.json',
        '--production-source-bundle', 'generated/base_translation_sources.bin',
        '--no-function-files', '--prune-stale', '--threads', "$([Environment]::ProcessorCount)")
    Run $dotnet @($cli, 'generate-data-init', '--project', $p)
    Run $dotnet @($cli, 'emit-build-shards', '--project', $p, '--base-metadata', 'generated/base_translation_output.json',
        '--base-functions-dir', 'generated/functions', '--native-source-dir', 'runtime/src', '--out', 'generated/build_shards')

    # --- Compile ------------------------------------------------------------------------
    # x86-64-v3 (AVX2) is faster; older CPUs get v2.
    Add-Type -Namespace EbBuild -Name Cpu -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(uint feature);'
    $cpuProfile = if ([EbBuild.Cpu]::IsProcessorFeaturePresent(40)) { 'v3' } else { 'v2' }
    $env:PATH = Get-MkwToolchainPath $tools
    $build = Join-Path $ws 'build\windows'
    # The game's own icon (the 48x48 save icon in files\mcbanner.res) replaces WiiCompiled's.
    $iconOut = Join-Path $ws 'runtime\resources\WiiCompiled.ico'
    try {
        Write-EtIcon (Join-Path $data 'files\mcbanner.res') $iconOut
        # The resource compiler does not track the .ico, so force the icon resource to rebuild:
        # touch the .rc and drop its old object (a build from an earlier setup kept the old icon).
        (Get-Item -LiteralPath (Join-Path $ws 'runtime\resources\WiiCompiled.rc')).LastWriteTime = Get-Date
        $iconObj = Join-Path $build 'CMakeFiles\WiiCompiled_icon.dir'
        if (Test-Path -LiteralPath $iconObj) { Remove-Item -LiteralPath $iconObj -Recurse -Force }
        Write-Host "Game icon written to $iconOut"
    } catch { Write-Warning "Could not make the game icon: $_" }
    # Files from the setup carry the build machine's clock, which can be hours ahead of this PC's.
    # A source dated in the future keeps build.ninja "dirty" forever (ninja gives up after 100
    # tries), so pull any such date back to now.
    $now = Get-Date
    Get-ChildItem -LiteralPath $ws -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -gt $now } |
        ForEach-Object { $_.LastWriteTime = $now }
    Step 'Configuring' '38'
    $configure = Get-MkwNativeConfigureArguments `
        -SourceDirectory (Join-Path $ws 'runtime') -BuildDirectory $build `
        -Ninja $ninja -CCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang.exe') `
        -CxxCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang++.exe') `
        -ResourceCompiler (Join-Path $bin 'x86_64-w64-mingw32-windres.exe') `
        -DependenciesDirectory $deps `
        -AdditionalArguments @(
            "-DMKW_INITIAL_PIPELINE_CACHE=$(if (Test-Path -LiteralPath $SeedCache -PathType Leaf) { ConvertTo-MkwCMakePath $SeedCache } else { 'NONE' })",
            "-DMKW_TRANSLATED_COMPILE_JOBS=$Jobs", "-DMKW_X86_CPU_PROFILE=$cpuProfile")
    Run $cmake $configure
    Step "Compiling with $Jobs jobs (this is the long part: 20 to 60 minutes)" '40 97'
    Run $cmake @('--build', $build, '--target', 'WiiCompiled', '--parallel', "$Jobs")
} finally {
    $env:PATH = $oldPath
    Pop-Location
}

# --- Package -----------------------------------------------------------------------------
Step "Installing to $Out" '98'
Copy-Item -LiteralPath (Join-Path $build 'WiiCompiled.exe') -Destination (Join-Path $Out 'Excitebots.exe') -Force
Get-ChildItem -LiteralPath $build -File |
    Where-Object { $_.Extension -in '.dll', '.bin', '.pem', '.db', '.zip' } |
    Copy-Item -Destination $Out -Force
foreach ($name in 'fonts', 'wii_bootstrap', 'config') {
    $directory = Join-Path $build $name
    if (Test-Path -LiteralPath $directory -PathType Container) {
        $target = Join-Path $Out $name
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        Copy-Item -LiteralPath $directory -Destination $target -Recurse
    }
}
$iconFile = Join-Path $ws 'runtime\resources\WiiCompiled.ico'
if (Test-Path -LiteralPath $iconFile -PathType Leaf) { Copy-Item -LiteralPath $iconFile -Destination (Join-Path $Out 'Excitebots.ico') -Force }
# Xbox button prompts (our own art, see tools/prompts/make_pack.py).
$prompts = if ($SetupMode) { Join-Path $Payload 'prompts' } else { Join-Path $Repo 'assets\prompts' }
$promptsTarget = Join-Path $Out 'prompts'
if (Test-Path -LiteralPath $promptsTarget) { Remove-Item -LiteralPath $promptsTarget -Recurse -Force }
if (Test-Path -LiteralPath $prompts -PathType Container) { Copy-Item -LiteralPath $prompts -Destination $promptsTarget -Recurse }
# portable.txt keeps settings, saves and caches in UserData next to the game.
New-Item -ItemType File -Force -Path (Join-Path $Out 'portable.txt') | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $Out 'UserData\achievements')) | Out-Null
$config = Join-Path $Out 'UserData\Config.toml'
if (-not (Test-Path -LiteralPath $config)) {
    $dvdRoot = if ($data -eq (Join-Path $Out 'Disc')) { '../Disc' } else { $data.Replace('\', '/') }
    [IO.File]::WriteAllText($config, "[paths]`ndvd_root = `"$dvdRoot`"`n")
}

if ($Shortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $desktop = [Environment]::GetFolderPath('Desktop')
    $link = $shell.CreateShortcut((Join-Path $desktop 'Excitebots.lnk'))
    # Straight into the game (players normally start it from the Excite PC launcher).
    $link.TargetPath = Join-Path $Out 'Excitebots.exe'
    $link.WorkingDirectory = $Out
    $gameIcon = Join-Path $Out 'Excitebots.ico'
    if (Test-Path -LiteralPath $gameIcon -PathType Leaf) { $link.IconLocation = "$gameIcon,0" }
    $link.Save()
    Write-Host 'Added an "Excitebots" shortcut to the desktop.'
}

Write-Host '##progress 100'
Write-Host ''
Write-Host "Done. The game is $(Join-Path $Out 'Excitebots.exe')." -ForegroundColor Green
Write-Host 'F10 opens the settings bar.'
try { Stop-Transcript | Out-Null } catch { }
