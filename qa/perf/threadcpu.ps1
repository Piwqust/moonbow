# Top threads by CPU time with their start module (who is burning CPU).
param([int]$ProcessId, [int]$Top = 12)
$p = Get-Process -Id $ProcessId
$mods = $p.Modules | ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Base = [int64]$_.BaseAddress; End = [int64]$_.BaseAddress + $_.ModuleMemorySize } }
if (-not ("TI2" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TI2 {
  [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(int a, bool i, int id);
  [DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(IntPtr h, int c, out IntPtr info, int len, IntPtr ret);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static long Start(int tid) { IntPtr h = OpenThread(0x0040, false, tid); if (h == IntPtr.Zero) return 0;
    IntPtr a; NtQueryInformationThread(h, 9, out a, IntPtr.Size, IntPtr.Zero); CloseHandle(h); return a.ToInt64(); }
}
"@ }
$p.Threads | ForEach-Object {
  $a = [TI2]::Start($_.Id); $m = $mods | Where-Object { $a -ge $_.Base -and $a -lt $_.End } | Select-Object -First 1
  [pscustomobject]@{ Tid = $_.Id; Ms = [math]::Round($_.TotalProcessorTime.TotalMilliseconds); Module = $(if ($m) { $m.Name } else { "?" }); Start = ('0x{0:X}' -f ($a - $(if ($m) { $m.Base } else { 0 }))) }
} | Sort-Object Ms -Descending | Select-Object -First $Top | Format-Table -AutoSize | Out-String
