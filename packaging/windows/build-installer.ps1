# Build FastCAE-<version>-Setup.exe with Inno Setup.
# Chay tu thu muc goc du an, trong "x64 Native Tools Command Prompt" / PowerShell co cmake + iscc.
#
#   powershell -ExecutionPolicy Bypass -File packaging\windows\build-installer.ps1
#
# Yeu cau: Visual Studio 2022, Qt 6 (MSVC kit), Inno Setup 6; tuy chon: gmsh.exe (gmsh.info) de nhap STEP/IGES (https://jrsoftware.org/isinfo.php)
param(
    [string]$Version = "1.0.0",
    [string]$BuildDir = "build",
    [string]$Dist = "dist",
    [string]$Toolchain = $env:VCPKG_TOOLCHAIN_FILE,
    # Ky so (giam canh bao SmartScreen). Chon MOT trong hai:
    [string]$SignPfx = "",                                   # file .pfx chung chi ky ma (mat khau: bien moi truong FASTCAE_SIGN_PASSWORD)
    [string]$SignCommand = "",                               # lenh ky tuy y co $f, vi du Azure Trusted Signing:  'signtool sign /v /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib "C:\...\Azure.CodeSigning.Dlib.dll" /dmdf "C:\...\metadata.json" $f'
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)
$ErrorActionPreference = "Stop"

$cfg = @("-S", ".", "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DFASTCAE_WITH_GMSH=OFF")
if ($Toolchain) { $cfg += "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" }
cmake @cfg
cmake --build $BuildDir --config Release
ctest --test-dir $BuildDir -C Release --output-on-failure

if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
cmake --install $BuildDir --config Release --prefix $Dist   # chay ca script gom DLL Qt

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw "Khong tim thay Inno Setup (ISCC.exe). Cai Inno Setup 6." }

$isccArgs = @("/DMyAppVersion=$Version", "/DSourceDir=$(Resolve-Path $Dist)")

# ---- Ky so (tuy chon) ----
$signTpl = ""
if ($SignCommand) { $signTpl = $SignCommand }
elseif ($SignPfx) {
    if (-not $env:FASTCAE_SIGN_PASSWORD) { throw "Dat bien moi truong FASTCAE_SIGN_PASSWORD (mat khau file .pfx)" }
    $signTpl = "signtool sign /v /fd SHA256 /tr $TimestampUrl /td SHA256 /f `"$((Resolve-Path $SignPfx).Path)`" /p `"$env:FASTCAE_SIGN_PASSWORD`" `$f"
}
if ($signTpl) {
    foreach ($exe in Get-ChildItem "$Dist\bin\*.exe") {      # ky tung file .exe truoc khi dong goi
        Invoke-Expression ($signTpl -replace '\$f', "`"$($exe.FullName)`"")
        if ($LASTEXITCODE -ne 0) { throw "Ky that bai: $($exe.Name)" }
    }
    $isccArgs += @("/DSignInstaller", "/Sfcsign=$signTpl")   # Inno ky bo cai va trinh go cai
} else {
    Write-Warning "Chua ky so: Windows SmartScreen se canh bao khi chay bo cai. Xem docs\RELEASE_WINDOWS.md"
}

& $iscc @isccArgs "packaging\windows\fastcae.iss"
$setup = "dist-installer\FastCAE-$Version-Setup.exe"
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
"$hash  FastCAE-$Version-Setup.exe" | Set-Content -Encoding ascii "$setup.sha256"
Write-Host "Xong: $setup"
Write-Host "SHA-256: $hash   (cong bo cung ban phat hanh de nguoi dung doi chieu)"
