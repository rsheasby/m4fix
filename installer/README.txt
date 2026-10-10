m4fix - fast navigation for Myst IV: Revelation
https://github.com/rsheasby/m4fix

Removes the long freeze after every click when moving around in Myst IV:
Revelation. It's an unofficial fix for the Steam and GOG releases. Quit the
game before installing.

WINDOWS
  Run Windows\m4fix-setup.exe. It finds the game and asks before changing
  anything. Run it again to update or uninstall.
  If SmartScreen appears: More info -> Run anyway.

MAC (Steam version)
  Double-click "Mac/Install m4fix.command". Run it again to uninstall.
  - If macOS won't open it: right-click it -> Open -> Open.
  - If it says macOS blocked changes to the game: System Settings ->
    Privacy & Security -> App Management -> allow Terminal, then try again.

MANUAL
  In the folder containing Myst4.exe, rename CrashRpt.dll to
  CrashRpt_orig.dll, then copy in the two files from "Manual install".

SETTINGS (m4fix.ini, next to Myst4.exe)
  TransitionDelayMs=100   pause after a click before moving (original: 550)
  LeadtoolsHeap=1         fast image memory
  SoundLocks=1            fast sound-engine locking
  Log=0                   1 = write m4fix.log

UNINSTALL
  Run the installer again, or rename CrashRpt_orig.dll back to CrashRpt.dll.
  Steam/GOG "verify files" also restores the original.

Not affiliated with Cyan Worlds or Ubisoft. MIT licence.
