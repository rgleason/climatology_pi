param([string]$HostRoot, [string]$Output, [string]$ExpectedVersion)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ClimatologyNative {
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
 [DllImport("user32.dll")] private static extern IntPtr GetMenu(IntPtr window);
 [DllImport("user32.dll")] private static extern int GetMenuItemCount(IntPtr menu);
 [DllImport("user32.dll")] private static extern IntPtr GetSubMenu(IntPtr menu, int position);
 [DllImport("user32.dll")] private static extern uint GetMenuItemID(IntPtr menu, int position);
 [DllImport("user32.dll", CharSet = CharSet.Unicode)]
 private static extern int GetMenuString(IntPtr menu, uint position, StringBuilder text, int length, uint flags);
 private static uint FindOptions(IntPtr menu) {
  for (int i = 0; i < GetMenuItemCount(menu); ++i) {
   var text = new StringBuilder(256);
   GetMenuString(menu, (uint)i, text, text.Capacity, 0x400);
   string label = text.ToString().Replace("&", "").Split('\t')[0];
   if (label.StartsWith("Options", StringComparison.Ordinal))
    return GetMenuItemID(menu, i);
   IntPtr submenu = GetSubMenu(menu, i);
   if (submenu != IntPtr.Zero) {
    uint id = FindOptions(submenu);
    if (id != UInt32.MaxValue) return id;
   }
  }
  return UInt32.MaxValue;
 }
 public static bool OpenOptions(IntPtr window) {
  uint id = FindOptions(GetMenu(window));
  return id != UInt32.MaxValue && PostMessage(window, 0x111, new IntPtr((long)id), IntPtr.Zero);
 }
}
"@
New-Item -ItemType Directory -Force $Output | Out-Null
function Capture([string]$Name) {
 $rect = [System.Windows.Forms.SystemInformation]::VirtualScreen
 $image = New-Object System.Drawing.Bitmap $rect.Width,$rect.Height
 $graphics = [System.Drawing.Graphics]::FromImage($image)
 try {
  $graphics.CopyFromScreen($rect.Location, [System.Drawing.Point]::Empty, $rect.Size)
  $image.Save((Join-Path $Output $Name), [System.Drawing.Imaging.ImageFormat]::Png)
 } finally { $graphics.Dispose(); $image.Dispose() }
}
function Nodes {
 $condition = [System.Windows.Automation.PropertyCondition]::new(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$app.Id)
 return [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
  [System.Windows.Automation.TreeScope]::Descendants, $condition)
}
$app = Start-Process -FilePath (Join-Path $HostRoot 'opencpn.exe') -WorkingDirectory $HostRoot -PassThru
try {
 $deadline = [DateTime]::UtcNow.AddSeconds(40)
 do {
  $app.Refresh()
  if ($app.HasExited) { throw 'OpenCPN exited at startup' }
  if ($app.MainWindowHandle -ne 0) { break }
  Start-Sleep -Milliseconds 250
 } while ([DateTime]::UtcNow -lt $deadline)
 Start-Sleep -Seconds 5
 foreach ($node in (Nodes)) {
  if ($node.Current.Name -eq 'Agree') {
   $node.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  }
 }
 # CircleCI's preinstalled Incredibuild tray can display a firewall prompt
 # which steals keyboard focus. Close only that known runner dialog.
 if ($env:CIRCLECI -eq 'true') {
  $condition = [System.Windows.Automation.PropertyCondition]::new(
   [System.Windows.Automation.AutomationElement]::NameProperty, 'Incredibuild Agent')
  foreach ($prompt in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $condition)) {
   [ClimatologyNative]::PostMessage([IntPtr]$prompt.Current.NativeWindowHandle,
    0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  }
 }
 $app.Refresh()
 [ClimatologyNative]::ShowWindow($app.MainWindowHandle, 3) | Out-Null
 [ClimatologyNative]::SetForegroundWindow($app.MainWindowHandle) | Out-Null
 # Invoke the real menu command without depending on foreground keyboard focus.
 if (-not [ClimatologyNative]::OpenOptions($app.MainWindowHandle)) {
  throw 'Could not invoke the OpenCPN Options menu command'
 }
 Start-Sleep -Seconds 2
 Capture 'options-open.png'
 @((Nodes) | ForEach-Object { $_.Current.Name }) | Set-Content -Encoding utf8 (Join-Path $Output 'options-text.txt')
 $deadline = [DateTime]::UtcNow.AddSeconds(30)
 $selected = $false
 do {
  foreach ($node in (Nodes)) {
   if ($node.Current.Name -eq 'Plugins') {
    try {
     $node.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
     $selected = $true
     break
    } catch {}
   }
  }
  if ($selected) { break }
  Start-Sleep -Milliseconds 250
 } while ([DateTime]::UtcNow -lt $deadline)
 if (-not $selected) { throw 'Could not select Options > Plugins' }
 Start-Sleep -Seconds 2
 $names = @((Nodes) | ForEach-Object { $_.Current.Name } | Where-Object { $_ })
 $names | Set-Content -Encoding utf8 (Join-Path $Output 'plugin-manager-text.txt')
 Capture 'plugin-manager-version.png'
 $joined = $names -join "`n"
 if ($joined -notmatch 'Climatology' -or $joined -notmatch [regex]::Escape($ExpectedVersion)) {
  throw "Plugin Manager does not display the expected Climatology $ExpectedVersion version"
 }
 # Close Options before requesting a normal application shutdown.
 foreach ($node in (Nodes)) {
  if ($node.Current.Name -eq 'Cancel') {
   try { $node.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); break } catch {}
  }
 }
 $quit = Start-Process -FilePath (Join-Path $HostRoot 'opencpn.exe') -ArgumentList '--remote','--quit' -WorkingDirectory $HostRoot -Wait -PassThru
 if ($quit.ExitCode -ne 0) { throw "Remote quit request failed: $($quit.ExitCode)" }
 if (-not $app.WaitForExit(30000)) { throw 'OpenCPN did not shut down normally' }
 if ($app.ExitCode -ne 0) { throw "OpenCPN shutdown failed: $($app.ExitCode)" }
 @{passed=$true; expected_version=$ExpectedVersion; displayed_text=$names; exit_code=$app.ExitCode} |
  ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $Output 'version-result.json')
} finally {
 try { Capture 'final.png' } catch {}
 if (-not $app.HasExited) { $app.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 1 }
 if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
}
