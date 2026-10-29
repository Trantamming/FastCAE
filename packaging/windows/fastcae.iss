; Inno Setup 6 script for FastCAE.
; Tác giả phần mềm: Trần Minh Tâm - Giấy phép MIT
; (File này lưu UTF-8 có BOM để Inno Setup 6 hiển thị đúng tiếng Việt - đừng bỏ BOM.)
;
; Build:
;   1) cmake --install build --config Release --prefix dist      (gom DLL Qt vao dist\bin)
;   2) iscc packaging\windows\fastcae.iss
;      hoac:  powershell packaging\windows\build-installer.ps1
; Tuy chon dong lenh:
;   iscc /DMyAppVersion=0.2.0 /DSourceDir=C:\path\dist packaging\windows\fastcae.iss
; Ky so (de giam canh bao SmartScreen - xem docs\RELEASE_WINDOWS.md): them /DSignInstaller va /Sfcsign="<lenh ky co $f>"
;   (build-installer.ps1 tu dong dung khi co -SignPfx hoac -SignCommand).
; Ket qua: dist-installer\FastCAE-<version>-Setup.exe

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef SourceDir
  ; thu muc cai dat do `cmake --install` tao ra (tuong doi so voi file .iss)
  #define SourceDir "..\..\dist"
#endif

#define MyAppName      "FastCAE"
#define MyAppPublisher "Trần Minh Tâm"
#define MyAppEmail     "tam.ming.zhan@gmail.com"
#define MyAppCopyright "Copyright (c) 2026 Trần Minh Tâm"
#define MyAppURL       "https://github.com/tranminhtam/FastCAE"
#define MyAppExeName   "fastcae.exe"
#define MyCliExeName   "fastcae-cli.exe"

[Setup]
; AppId la dinh danh duy nhat de nang cap/go cai dat. KHONG doi sau khi phat hanh.
AppId={{7B3C1E52-6F0A-4D55-9A47-3C1B9E0F6A21}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppContact={#MyAppEmail}
AppComments=Phần mềm CAE mã nguồn mở. Tác giả: {#MyAppPublisher} ({#MyAppEmail})
AppCopyright={#MyAppCopyright}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright={#MyAppCopyright}
VersionInfoDescription=FastCAE - Phần mềm CAE mã nguồn mở
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
MinVersion=10.0
#ifdef SignInstaller
SignTool=fcsign
SignedUninstaller=yes
#endif
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
InfoBeforeFile=..\..\THIRD_PARTY_NOTICES.txt
OutputDir=..\..\dist-installer
OutputBaseFilename=FastCAE-{#MyAppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
ChangesAssociations=yes
ChangesEnvironment=yes
CloseApplications=yes
#ifexist "..\..\resources\fastcae.ico"
SetupIconFile=..\..\resources\fastcae.ico
#endif

[Messages]
BeveledLabel=FastCAE · Tác giả: Trần Minh Tâm ({#MyAppEmail}) · MIT

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
; Ban dich tieng Viet khong di kem Inno Setup. Tai Vietnamese.isl tu
; https://jrsoftware.org/files/istrans/ va dat vao packaging\windows\ de bat.
#ifexist "Vietnamese.isl"
Name: "vietnamese"; MessagesFile: "Vietnamese.isl"
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assoc";       Description: "Liên kết file bài toán .fcae với FastCAE"; GroupDescription: "Tích hợp hệ thống:"
Name: "addtopath";   Description: "Thêm thư mục bin vào PATH (dùng fastcae-cli trong dòng lệnh)"; GroupDescription: "Tích hợp hệ thống:"; Flags: unchecked

[Files]
; Toan bo cay cai dat do CMake tao (bin\, share\fastcae\examples, tools, doc...).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\scripts\*.lua"; DestDir: "{app}\scripts"; Flags: ignoreversion
Source: "..\..\python\fastcae.py"; DestDir: "{app}\python"; Flags: ignoreversion
Source: "..\..\examples\python\*.py"; DestDir: "{app}\examples\python"; Flags: ignoreversion
Source: "..\..\docs\SCRIPTING.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\..\docs\ARCHITECTURE.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\..\third_party\lua\LICENSE.txt"; DestDir: "{app}"; DestName: "LICENSE-Lua.txt"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\third_party\licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\..\docs\LICENSES.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#MyAppName}";                  Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\Ví dụ (examples)";              Filename: "{app}\share\fastcae\examples"
Name: "{group}\Bài thực hành (hướng dẫn từng bước)"; Filename: "{app}\share\fastcae\examples\tutorial"
Name: "{group}\Solver tự lập trình (plugin)"; Filename: "{app}\share\fastcae\solvers.d"
Name: "{group}\Giấy phép và tác giả"; Filename: "{app}\THIRD_PARTY_NOTICES.txt"
Name: "{group}\Văn bản giấy phép (LGPL, GPL)"; Filename: "{app}\licenses"
Name: "{group}\Gỡ cài đặt {#MyAppName}";       Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";            Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.fcae";                               ValueType: string; ValueName: ""; ValueData: "FastCAE.Case"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\FastCAE.Case";                        ValueType: string; ValueName: ""; ValueData: "FastCAE case file - Trần Minh Tâm"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\FastCAE.Case\DefaultIcon";            ValueType: string; ValueName: ""; ValueData: "{app}\bin\{#MyAppExeName},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\FastCAE.Case\shell\open\command";     ValueType: string; ValueName: ""; ValueData: """{app}\bin\{#MyAppExeName}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  EnvKeyAdmin = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  EnvKeyUser  = 'Environment';

function EnvRoot(): Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function EnvKey(): String;
begin
  if IsAdminInstallMode then Result := EnvKeyAdmin else Result := EnvKeyUser;
end;

procedure AddToPath(const Dir: String);
var
  Paths: String;
begin
  if not RegQueryStringValue(EnvRoot(), EnvKey(), 'Path', Paths) then Paths := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then Exit;  // da co
  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then Paths := Paths + ';';
  RegWriteExpandStringValue(EnvRoot(), EnvKey(), 'Path', Paths + Dir);
end;

procedure RemoveFromPath(const Dir: String);
var
  Paths, P: String;
  N: Integer;
begin
  if not RegQueryStringValue(EnvRoot(), EnvKey(), 'Path', Paths) then Exit;
  P := ';' + Paths + ';';
  N := Pos(';' + Uppercase(Dir) + ';', Uppercase(P));
  if N = 0 then Exit;
  Delete(P, N, Length(Dir) + 1);
  P := Copy(P, 2, Length(P) - 2);
  RegWriteExpandStringValue(EnvRoot(), EnvKey(), 'Path', P);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}\bin'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
