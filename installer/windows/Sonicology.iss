; Sonicology for Windows: one installer for Voxology (+ Honey Tune), Mixology (+ Mixology Link) and Masterology.
; Built by .github/workflows/sonicology.yml:  ISCC /DStage=<staged plug-ins> /DSuiteVersion=0.1.0 /DLabel="Beta 1"
; The plug-ins go where every DAW looks for VST3 (C:\Program Files\Common Files\VST3), the bridge scripts into the
; user's Cubase, FL Studio and Ableton Live folders. Uninstall removes what it put there.

#ifndef Stage
  #define Stage "..\..\stage"
#endif
#ifndef SuiteVersion
  #define SuiteVersion "0.1.0"
#endif
#ifndef Label
  #define Label "Beta"
#endif

[Setup]
AppId={{213FCB0C-F017-437C-9406-1A46E21D0F90}
AppName=Sonicology
AppVersion={#SuiteVersion}
AppVerName=Sonicology {#Label}
AppPublisher=Sonicology
DefaultDirName={commonpf64}\Sonicology
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\..\out
OutputBaseFilename=Sonicology-Setup-Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
WizardImageFile=..\art\wizard-100.bmp,..\art\wizard-200.bmp
WizardSmallImageFile=..\art\small-100.bmp,..\art\small-200.bmp
InfoBeforeFile={#Stage}\Read Me First.txt
UninstallDisplayName=Sonicology
Compression=lzma2/max
SolidCompression=yes

[Messages]
SelectComponentsLabel2=Select the components you want to install; clear the components you do not want to install. Click Next when you are ready to continue.%n%nLogic Pro and Cakewalk need no script: Mixology works as their Mackie Control (the Read Me says how).

[Types]
Name: "full"; Description: "Everything (recommended)"
Name: "custom"; Description: "Pick what to install"; Flags: iscustom

[Components]
Name: "voxology"; Description: "Voxology + Honey Tune (vocals)"; Types: full
Name: "mixology"; Description: "Mixology + Mixology Link (mixing)"; Types: full
Name: "masterology"; Description: "Masterology (mastering)"; Types: full
Name: "cubase"; Description: "Cubase bridge script (lets Mixology move Cubase's faders)"; Types: full
Name: "flstudio"; Description: "FL Studio bridge script (lets Mixology move FL Studio's faders)"; Types: full
Name: "ableton"; Description: "Ableton Live bridge script (lets Mixology move Live's faders)"; Types: full

[Files]
Source: "{#Stage}\Voxology\VST3\*"; DestDir: "{commoncf64}\VST3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: voxology
Source: "{#Stage}\Mixology\VST3\*"; DestDir: "{commoncf64}\VST3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: mixology
Source: "{#Stage}\Masterology\VST3\*"; DestDir: "{commoncf64}\VST3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: masterology
Source: "{#Stage}\Mixology\Cubase MIDI Remote\Mixology\*"; DestDir: "{userdocs}\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\Mixology"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: cubase
Source: "{#Stage}\Mixology\FL Studio\Mixology Bridge\*"; DestDir: "{userdocs}\Image-Line\FL Studio\Settings\Hardware\Mixology Bridge"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: flstudio
Source: "{#Stage}\Mixology\Ableton\MixologyBridge\*"; DestDir: "{userdocs}\Ableton\User Library\Remote Scripts\MixologyBridge"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: ableton
Source: "{#Stage}\Read Me First.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Manuals\*"; DestDir: "{app}\Manuals"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Sonicology\Read Me First"; Filename: "{app}\Read Me First.txt"
Name: "{autoprograms}\Sonicology\Manuals"; Filename: "{app}\Manuals"
Name: "{autoprograms}\Sonicology\Uninstall Sonicology"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Read Me First.txt"; Description: "Open the Read Me"; Flags: postinstall shellexec skipifsilent
Filename: "https://www.tobias-erichsen.de/software/loopmidi.html"; Description: "Get loopMIDI (free): Mixology needs it to move Cubase's faders"; Flags: postinstall shellexec skipifsilent unchecked
