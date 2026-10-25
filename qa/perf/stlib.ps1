Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class St {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && c.ToString() == "MoonbowHost" && GetWindow(h, 4) == IntPtr.Zero) { f = h; return false; } return true; }, IntPtr.Zero); return f; }
  public static void Wheel(IntPtr h, int x, int y, int delta, int count, int sleepMs) {
    var pt = new POINT { X = x, Y = y }; ClientToScreen(h, ref pt);
    for (int i = 0; i < count; i++) { PostMessage(h, 0x20A, (IntPtr)((delta * 120) << 16), (IntPtr)((pt.Y << 16) | (pt.X & 0xFFFF))); System.Threading.Thread.Sleep(sleepMs); } }
  public static void Hover(IntPtr h, int x0, int y0, int x1, int y1, int steps, int sleepMs) {
    for (int i = 0; i <= steps; i++) { int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
      PostMessage(h, 0x200, IntPtr.Zero, (IntPtr)((y << 16) | (x & 0xFFFF))); System.Threading.Thread.Sleep(sleepMs); } }
}
"@
