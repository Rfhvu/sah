param([string]$action = "shot", [int]$cx = 0, [int]$cy = 0, [string]$out = "shot.png")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$proc = Get-Process -Name MonumentDemo -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($null -eq $proc) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
$h = $proc.MainWindowHandle
if ($action -eq "key") {
    $vk = [int][char]$out
    [W]::PostMessage($h, 0x0100, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 400
    Write-Output "KEY_SENT $out"
    exit 0
}
if ($action -eq "click") {
    $lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
    [W]::PostMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null
    [W]::PostMessage($h, 0x0202, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 600
    Write-Output "CLICK $cx,$cy"
    exit 0
}
# screenshot: client area only
$r = New-Object W+RECT
[W]::GetClientRect($h, [ref]$r) | Out-Null
$p = New-Object W+POINT
$p.X = 0; $p.Y = 0
[W]::ClientToScreen($h, [ref]$p) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($p.X, $p.Y, 0, 0, (New-Object System.Drawing.Size $w, $ht))
$full = Join-Path (Get-Location) $out
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "SAVED $full ($w x $ht)"
