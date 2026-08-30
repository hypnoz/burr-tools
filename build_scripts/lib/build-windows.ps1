# Build BurrTools on Windows (MinGW via MSYS2).
#   powershell -ExecutionPolicy Bypass -File build_scripts\lib\build-windows.ps1
# or double-click / run:  build_scripts\build-windows.bat
#
# meson.build links with GCC flags (-static-libgcc), so this script uses
# MSYS2 MinGW-w64, not MSVC.

param(
	[ValidateSet("release", "debug", "debugoptimized", "minsize", "plain")]
	[string]$BuildType = "release",
	[string]$BuildDir = "build",
	[switch]$SkipDepInstall,
	[switch]$SkipPackage,
	[switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
	@"
Build BurrTools on Windows (meson + ninja, MinGW-w64) and create a zip.

Usage:
  build-windows.ps1 [-BuildType release] [-BuildDir build]
                    [-SkipDepInstall] [-SkipPackage]

This project does not build with MSVC. If meson/ninja/g++ are not already
on PATH, the script installs MSYS2 (via winget when available) and the
mingw-w64 toolchain.

Run from cmd.exe or Explorer with build-windows.bat if PowerShell
execution policy blocks .ps1 files.
"@
}

if ($Help) {
	Show-Usage
	exit 0
}

function Write-Info([string]$Message) { Write-Host $Message }
function Write-Warn([string]$Message) { Write-Warning $Message }
function Die([string]$Message) {
	Write-Host "Error: $Message" -ForegroundColor Red
	exit 1
}

function Test-Command([string]$Name) {
	return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
Set-Location $Root

function Test-MesonVersion {
	param([string]$Minimum = "0.56")
	if (-not (Test-Command "meson")) { return $false }
	$ver = (& meson --version 2>$null | Select-Object -First 1)
	if (-not $ver) { return $false }
	try {
		return ([version]$ver -ge [version]$Minimum)
	} catch {
		return $true
	}
}

function Find-Msys2 {
	$candidates = @(
		"C:\msys64",
		(Join-Path $env:SystemDrive "msys64"),
		(Join-Path $env:USERPROFILE "msys64"),
		(Join-Path $env:ProgramFiles "MSYS2"),
		(Join-Path $env:ProgramFiles "msys64")
	)
	if ($env:MSYS2_ROOT) {
		$candidates = @($env:MSYS2_ROOT) + $candidates
	}
	foreach ($dir in $candidates) {
		if ($dir -and (Test-Path (Join-Path $dir "usr\bin\bash.exe"))) {
			return $dir
		}
	}
	return $null
}

function Use-MingwPath([string]$MsysRoot) {
	$mingwBin = Join-Path $MsysRoot "mingw64\bin"
	$usrBin = Join-Path $MsysRoot "usr\bin"
	$env:MSYSTEM = "MINGW64"
	$env:PATH = "$mingwBin;$usrBin;$env:PATH"
}

function Invoke-MsysPacman {
	param(
		[string]$MsysRoot,
		[string[]]$Packages
	)
	$bash = Join-Path $MsysRoot "usr\bin\bash.exe"
	$pkgLine = ($Packages -join " ")
	Write-Info "Installing MSYS2 packages: $pkgLine"
	& $bash -lc "pacman -Sy --noconfirm"
	if ($LASTEXITCODE -ne 0) {
		throw "pacman -Sy failed with exit code $LASTEXITCODE"
	}
	& $bash -lc "pacman -S --needed --noconfirm $pkgLine"
	if ($LASTEXITCODE -ne 0) {
		throw "pacman failed with exit code $LASTEXITCODE"
	}
}

function Install-Msys2 {
	if (-not (Test-Command "winget")) {
		return $false
	}
	Write-Info "Installing MSYS2 with winget (may prompt for User Account Control)..."
	& winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
	# 0 = installed; -1978335189 = already installed
	if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
		Write-Warn "winget install MSYS2.MSYS2 exited with code $LASTEXITCODE"
		return $false
	}
	return $true
}

function Show-ManualWindowsDeps {
	@"
BurrTools on Windows needs a MinGW-w64 toolchain (not Visual Studio / MSVC):

  meson (>= 0.56), ninja, cmake, git, g++ (x86_64-w64-mingw32)

Recommended: install MSYS2 from https://www.msys2.org/ then in a
MINGW64 shell:

  pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf git

If winget is available this script can install MSYS2 for you. Re-run
from an elevated prompt if the installer is blocked.
"@
}

function Test-MingwReady {
	$ok = (Test-Command "g++") -and (Test-Command "meson") -and (Test-Command "ninja") -and (Test-Command "cmake") -and (Test-Command "git")
	if (-not $ok) { return $false }
	$compiler = (& g++ --version 2>$null | Select-Object -First 1)
	if ($compiler -notmatch "g\+\+") {
		return $false
	}
	return $true
}

function Ensure-Dependencies {
	if (Test-MingwReady -and (Test-MesonVersion)) {
		Write-Info "Found MinGW build tools on PATH."
		return
	}

	$msys = Find-Msys2
	if ($msys) {
		Use-MingwPath $msys
		if (Test-MingwReady -and (Test-MesonVersion)) {
			Write-Info "Using MSYS2 MinGW64 at $msys"
			return
		}
	}

	if ($SkipDepInstall) {
		Write-Host (Show-ManualWindowsDeps)
		Die "Build tools missing and -SkipDepInstall was set."
	}

	if (-not $msys) {
		if (Install-Msys2) {
			for ($i = 0; $i -lt 15 -and -not $msys; $i++) {
				Start-Sleep -Seconds 1
				$msys = Find-Msys2
			}
		}
		if (-not $msys) {
			Write-Host (Show-ManualWindowsDeps)
			Die "MSYS2 was not found. Install it from https://www.msys2.org/ and re-run."
		}
	}

	Use-MingwPath $msys
	try {
		Invoke-MsysPacman -MsysRoot $msys -Packages @(
			"mingw-w64-x86_64-gcc",
			"mingw-w64-x86_64-meson",
			"mingw-w64-x86_64-ninja",
			"mingw-w64-x86_64-cmake",
			"mingw-w64-x86_64-pkgconf",
			"git"
		)
	} catch {
		Write-Host (Show-ManualWindowsDeps)
		Die "Could not install MinGW packages via pacman: $_"
	}

	Use-MingwPath $msys
	if (-not (Test-MingwReady)) {
		Write-Host (Show-ManualWindowsDeps)
		Die "g++, meson, ninja, cmake, and git must be on PATH after the MSYS2 install."
	}
	if (-not (Test-MesonVersion)) {
		Die "meson on PATH is older than 0.56. Upgrade the mingw-w64-x86_64-meson package."
	}
	Write-Info "Using MSYS2 MinGW64 at $msys"
}

Ensure-Dependencies

$gxx = (& g++ --version | Select-Object -First 1)
Write-Info "Compiler: $gxx"
Write-Info "meson $((meson --version).Trim())"

$Version = "dev"
if (Test-Command "git") {
	$described = & git describe --tags --always --dirty 2>$null
	if ($LASTEXITCODE -eq 0 -and $described) {
		$Version = $described.Trim()
	}
}

Write-Info "Building BurrTools $Version ($BuildType) in $BuildDir"

$setupArgs = @("setup", $BuildDir, "--buildtype=$BuildType")
if (Test-Path (Join-Path $BuildDir "build.ninja")) {
	$setupArgs += "--reconfigure"
}
& meson @setupArgs
if ($LASTEXITCODE -ne 0) { Die "meson setup failed." }

& meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) { Die "meson compile failed." }

$exes = @("burrtools.exe", "burrTxt.exe", "burrTxt2.exe")
foreach ($exe in $exes) {
	$path = Join-Path $BuildDir $exe
	if (-not (Test-Path $path)) {
		Die "Build finished but $path is missing."
	}
}

if (Test-Command "strip") {
	& strip @($exes | ForEach-Object { Join-Path $BuildDir $_ })
}

Write-Info "Binaries:"
foreach ($exe in $exes) {
	Write-Info ("  " + (Join-Path $BuildDir $exe))
}

if ($SkipPackage) { exit 0 }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("burrtools-pack-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
	foreach ($exe in $exes) {
		Copy-Item (Join-Path $BuildDir $exe) $stage
	}
	if (Test-Path "examples") { Copy-Item "examples" (Join-Path $stage "examples") -Recurse }
	if (Test-Path "README.md") { Copy-Item "README.md" $stage }
	if (Test-Path "COPYING") { Copy-Item "COPYING" $stage }

	$archive = Join-Path $Root "burrtools-$Version-windows-x86_64.zip"
	if (Test-Path $archive) { Remove-Item $archive -Force }
	$zipItems = @(Get-ChildItem -Path $stage | ForEach-Object { $_.FullName })
	Compress-Archive -Path $zipItems -DestinationPath $archive
	Write-Info "Created $archive"
} finally {
	Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Info "Run the GUI with: $(Join-Path $BuildDir 'burrtools.exe')"
