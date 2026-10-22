; Lumen installer for one user, no admin rights: %LOCALAPPDATA%\Programs\Lumen, a Start menu
; shortcut, file types via Lumen.exe --register-file-types, a ticked "default video player" box on the
; last page (--set-default-player). Uninstall runs --unregister-file-types
; and offers to delete %APPDATA%\Lumen. A newer setup installs over the old one, data kept.
; Built by tools\package-beta.ps1 with Inno Setup 6:
;   ISCC /DAppVersion=0.1.0 /DBuild=<commit> /DSrc=<folder with the files> /DOut=<output folder> lumen.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Build
  #define Build "dev"
#endif

[Setup]
; Never change AppId: updates and uninstall find the installed Lumen by it.
AppId={{0E08DF8D-068E-44DF-96DD-AFDC2CCD259A}
AppName=Lumen
AppVersion={#AppVersion}
AppVerName=Lumen {#AppVersion} beta ({#Build})
AppPublisher=Lumen
DefaultDirName={autopf}\Lumen
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Out}
OutputBaseFilename=Lumen-{#AppVersion}-beta-{#Build}-setup
SetupIconFile=..\..\res\lumen.ico
UninstallDisplayIcon={app}\Lumen.exe
UninstallDisplayName=Lumen
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
; Lumen's single-instance mutex (src/main.cpp): setup and uninstall ask to close a running Lumen.
AppMutex=Lumen.SingleInstance.v2
CloseApplications=yes
ChangesAssociations=yes

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.Registering=Регистрация видео и аудио форматов…
en.Registering=Registering video and audio types…
ru.DefaultPlayer=Открывать видео в Lumen по умолчанию
en.DefaultPlayer=Open videos in Lumen by default
ru.RemoveData=Удалить также настройки, медиатеку, журналы и отчёты о сбоях Lumen (папка %APPDATA%\Lumen)?%n%nЕсли собираетесь поставить Lumen снова, выберите «Нет».
en.RemoveData=Also delete Lumen's settings, library, logs and crash reports (the %APPDATA%\Lumen folder)?%n%nChoose No if you plan to install Lumen again.

[Files]
Source: "{#Src}\Lumen.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Src}\libmpv-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Src}\README-BETA.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Lumen"; Filename: "{app}\Lumen.exe"

[Run]
; Also on updates: the registration points at this folder's Lumen.exe.
Filename: "{app}\Lumen.exe"; Parameters: "--register-file-types"; StatusMsg: "{cm:Registering}"; Flags: runhidden waituntilterminated
; Writes Windows' per-user choice (src/app/FileAssociations.h); opens Default apps where Windows refuses it.
Filename: "{app}\Lumen.exe"; Parameters: "--set-default-player"; Description: "{cm:DefaultPlayer}"; Flags: postinstall runhidden waituntilterminated skipifsilent
Filename: "{app}\Lumen.exe"; Description: "{cm:LaunchProgram,Lumen}"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{app}\Lumen.exe"; Parameters: "--unregister-file-types"; RunOnceId: "UnregisterFileTypes"; Flags: runhidden waituntilterminated

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // A silent uninstall keeps the data: only a person who said yes loses their library.
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent and
     (MsgBox(CustomMessage('RemoveData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    DelTree(ExpandConstant('{userappdata}\Lumen'), True, True, True);
end;
