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
; A spare copy of the scripts beside the Read Me: for a DAW whose folder isn't the usual one, or an installer run from
; another (admin) account, whose Documents the copies above went to.
Source: "{#Stage}\Mixology\Cubase MIDI Remote\Mixology\*"; DestDir: "{app}\Bridge scripts\Cubase\Mixology"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: cubase
Source: "{#Stage}\Mixology\FL Studio\Mixology Bridge\*"; DestDir: "{app}\Bridge scripts\FL Studio\Mixology Bridge"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: flstudio
Source: "{#Stage}\Mixology\Ableton\MixologyBridge\*"; DestDir: "{app}\Bridge scripts\Ableton\MixologyBridge"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: ableton
Source: "{#Stage}\Read Me First.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Manuals\*"; DestDir: "{app}\Manuals"; Flags: recursesubdirs ignoreversion
; The header's Sonicology title, in a few sizes for the screen's scaling (used by the [Code] below, not installed)
Source: "..\art\header-100.bmp"; Flags: dontcopy
Source: "..\art\header-150.bmp"; Flags: dontcopy
Source: "..\art\header-200.bmp"; Flags: dontcopy
Source: "..\art\header-270.bmp"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\Sonicology\Read Me First"; Filename: "{app}\Read Me First.txt"
Name: "{autoprograms}\Sonicology\Manuals"; Filename: "{app}\Manuals"
Name: "{autoprograms}\Sonicology\Uninstall Sonicology"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Read Me First.txt"; Description: "Open the Read Me"; Flags: postinstall shellexec skipifsilent
Filename: "https://www.tobias-erichsen.de/software/loopmidi.html"; Description: "Get loopMIDI (free): Mixology needs it to move faders in Cubase, FL Studio, Live and Cakewalk"; Flags: postinstall shellexec skipifsilent unchecked

[InstallDelete]
; Masterology's first name was Polisher, with the same plug-in IDs: an old Polisher.vst3 is the same plug-in to the
; DAW (it might load the old one), so it goes.
Type: filesandordirs; Name: "{commoncf64}\VST3\Polisher.vst3"; Components: masterology

[UninstallDelete]
; FL Studio and Live leave compiled copies of the Python scripts next to them.
Type: filesandordirs; Name: "{userdocs}\Image-Line\FL Studio\Settings\Hardware\Mixology Bridge\__pycache__"; Components: flstudio
Type: filesandordirs; Name: "{userdocs}\Ableton\User Library\Remote Scripts\MixologyBridge\__pycache__"; Components: ableton
Type: dirifempty; Name: "{userdocs}\Image-Line\FL Studio\Settings\Hardware\Mixology Bridge"; Components: flstudio
Type: dirifempty; Name: "{userdocs}\Ableton\User Library\Remote Scripts\MixologyBridge"; Components: ableton

[Code]
// The header of the inner pages: dark and a little taller, with the whole Sonicology title on the right
// (Inno's own header only has room for a small square picture). The page's name and description stay on the left.
procedure InitializeWizard;
var
  Logo: TBitmapImage;
  Grow, H, W, Room: Integer;
  Name: String;
begin
  Grow := ScaleY(40);
  WizardForm.MainPanel.Height := WizardForm.MainPanel.Height + Grow;
  WizardForm.Bevel1.Top := WizardForm.Bevel1.Top + Grow;
  WizardForm.InnerNotebook.Top := WizardForm.InnerNotebook.Top + Grow;
  WizardForm.InnerNotebook.Height := WizardForm.InnerNotebook.Height - Grow;

  WizardForm.MainPanel.Color := clBlack;
  WizardForm.WizardSmallBitmapImage.Visible := False;
  WizardForm.PageNameLabel.Font.Color := clWhite;
  WizardForm.PageDescriptionLabel.Font.Color := $C8C8C8;
  WizardForm.PageNameLabel.Top := WizardForm.PageNameLabel.Top + Grow div 2;
  WizardForm.PageDescriptionLabel.Top := WizardForm.PageDescriptionLabel.Top + Grow div 2;

  // The title fills the header's height, but never more than about half its width.
  H := WizardForm.MainPanel.Height - ScaleY(6);
  W := H * 214 div 100;
  Room := WizardForm.MainPanel.ClientWidth * 55 div 100;
  if W > Room then
  begin
    W := Room;
    H := W * 100 div 214;
  end;
  if H <= 100 then Name := 'header-100.bmp'
  else if H <= 150 then Name := 'header-150.bmp'
  else if H <= 200 then Name := 'header-200.bmp'
  else Name := 'header-270.bmp';
  ExtractTemporaryFile(Name);

  Logo := TBitmapImage.Create(WizardForm);
  Logo.Parent := WizardForm.MainPanel;
  Logo.Stretch := True;
  Logo.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + Name);
  Logo.SetBounds(WizardForm.MainPanel.ClientWidth - W - ScaleX(6), (WizardForm.MainPanel.Height - H) div 2, W, H);
  Logo.Anchors := [akTop, akRight];

  WizardForm.PageNameLabel.Width := Logo.Left - WizardForm.PageNameLabel.Left - ScaleX(10);
  WizardForm.PageDescriptionLabel.Width := Logo.Left - WizardForm.PageDescriptionLabel.Left - ScaleX(10);
end;
