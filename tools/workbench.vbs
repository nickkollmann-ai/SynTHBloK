' SynTHBloK Workbench launcher: runs tools\open_workbench.ps1 with no console window at all (the shortcuts point here).
Set sh = CreateObject("WScript.Shell")
dir = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)
sh.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & dir & "\open_workbench.ps1""", 0, False
