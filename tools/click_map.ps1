Add-Type -MemberDefinition @'
[DllImport("user32.dll")]
public static extern void mouse_event(int dwFlags, int dx, int dy, int dwData, int dwExtraInfo);
[DllImport("user32.dll")]
public static extern bool SetCursorPos(int x, int y);
'@ -Name "InputUtil" -Namespace "GtaInput"

$ws = New-Object -ComObject WScript.Shell
$ws.AppActivate("GTA: San Andreas")
Start-Sleep -Milliseconds 300
$ws.SendKeys("{ESC}")
Start-Sleep -Milliseconds 600
$ws.SendKeys("{DOWN}{DOWN}{ENTER}")
Start-Sleep -Milliseconds 1200
[GtaInput.InputUtil]::SetCursorPos(750, 400)
Start-Sleep -Milliseconds 300
[GtaInput.InputUtil]::mouse_event(0x0008, 0, 0, 0, 0)
Start-Sleep -Milliseconds 100
[GtaInput.InputUtil]::mouse_event(0x0010, 0, 0, 0, 0)
Start-Sleep -Milliseconds 600
$ws.SendKeys("t")
Start-Sleep -Milliseconds 600
$ws.SendKeys("{ESC}")
Start-Sleep -Milliseconds 400
$ws.SendKeys("{ESC}")
Start-Sleep -Milliseconds 400
$ws.SendKeys("{ENTER}")
Start-Sleep -Seconds 2
powershell -ExecutionPolicy Bypass -File "C:\Users\me\dev\sa_project\gta-sa-mcp\tools\screenshot.ps1"
