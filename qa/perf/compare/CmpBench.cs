// Outside-in measurements of a media player window, the same for every player:
// it starts the process, reads the window's pixels with PrintWindow and decodes the
// marks that qa/perf/compare/make-media.sh draws into each frame (a colour square and
// the frame number in binary under it). No hooks, no player-specific API.
// Compiled by bench.ps1 with Add-Type (C# 5).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class CmpBench {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int pid, tid; }
    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFOHEADER {
        public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
    }
    delegate bool EnumProc(IntPtr h, IntPtr l);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                      IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll", EntryPoint = "PostMessageW")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFOHEADER bi, uint usage, out IntPtr bits, IntPtr sec, uint off);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);

    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    static long t0, tMark;
    static IntPtr hProc = IntPtr.Zero;
    public static int Pid;
    static IntPtr memDC = IntPtr.Zero, dib = IntPtr.Zero, bits = IntPtr.Zero;
    static int bw, bh;
    // The square found in the last capture: centre, side, colour class, brightest channel.
    static int sqX, sqY, sqS, sqC, sqMax;
    static bool located;

    public static void Init() { SetProcessDPIAware(); timeBeginPeriod(1); }
    static double Since(long from) { return (Stopwatch.GetTimestamp() - from) * 1000.0 / Stopwatch.Frequency; }
    public static double Ms() { return Since(t0); }
    static string F(double v) { return v.ToString("0.0", Inv); }

    // ---- process ---------------------------------------------------------------------------
    public static int Launch(string cmdline, string cwd) {
        Kill();
        located = false;
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.dwFlags = 1;        // STARTF_USESHOWWINDOW
        si.wShowWindow = 4;    // SW_SHOWNOACTIVATE: players that honour it don't take the focus
        PROCESS_INFORMATION pi;
        StringBuilder cmd = new StringBuilder(cmdline, cmdline.Length + 2);
        t0 = Stopwatch.GetTimestamp();
        if (!CreateProcessW(null, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, cwd, ref si, out pi))
            throw new Exception("CreateProcess failed: " + Marshal.GetLastWin32Error() + " for " + cmdline);
        CloseHandle(pi.hThread);
        hProc = pi.hProcess;
        Pid = pi.pid;
        return Pid;
    }
    public static bool Exited() { return hProc == IntPtr.Zero || WaitForSingleObject(hProc, 0) == 0; }
    public static void Kill() {
        if (hProc == IntPtr.Zero) return;
        if (WaitForSingleObject(hProc, 0) != 0) { TerminateProcess(hProc, 1); WaitForSingleObject(hProc, 3000); }
        CloseHandle(hProc);
        hProc = IntPtr.Zero;
    }
    // WM_CLOSE to the main window; ms until the process is gone, or -1 (then it is killed).
    public static double CloseAndWait(int timeoutMs) {
        IntPtr h = MainWindow();
        if (h == IntPtr.Zero || hProc == IntPtr.Zero) { Kill(); return -1; }
        long from = Stopwatch.GetTimestamp();
        PostMessageW(h, 0x0010, IntPtr.Zero, IntPtr.Zero);
        bool gone = WaitForSingleObject(hProc, (uint)timeoutMs) == 0;
        double ms = Since(from);
        Kill();
        return gone ? ms : -1;
    }

    // ---- window ----------------------------------------------------------------------------
    static IntPtr best; static long bestArea; static uint wantPid;
    static readonly EnumProc enumCb = EnumCb;
    static bool EnumCb(IntPtr h, IntPtr l) {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p != wantPid || !IsWindowVisible(h) || IsIconic(h)) return true;
        if (GetWindow(h, 4) != IntPtr.Zero) return true;   // owned: dialogs, tool windows
        RECT r; GetWindowRect(h, out r);
        int w = r.R - r.L, hh = r.B - r.T;
        if (w < 300 || hh < 200) return true;
        if ((long)w * hh > bestArea) { bestArea = (long)w * hh; best = h; }
        return true;
    }
    public static IntPtr MainWindow() {
        best = IntPtr.Zero; bestArea = 0; wantPid = (uint)Pid;
        EnumWindows(enumCb, IntPtr.Zero);
        return best;
    }
    public static string WindowInfo() {
        IntPtr h = MainWindow();
        if (h == IntPtr.Zero) return "no window";
        RECT r; GetWindowRect(h, out r);
        StringBuilder c = new StringBuilder(128); GetClassNameW(h, c, 128);
        return c + " " + (r.R - r.L) + "x" + (r.B - r.T) + " at " + r.L + "," + r.T;
    }
    // Same outer size for every player, without activating or re-ordering the window.
    public static void Resize(int w, int h) {
        IntPtr win = MainWindow();
        if (win != IntPtr.Zero) SetWindowPos(win, IntPtr.Zero, 0, 0, w, h, 0x0002 | 0x0004 | 0x0010);  // NOMOVE|NOZORDER|NOACTIVATE
    }
    // Puts the main window somewhere else (another monitor) as soon as it exists; no activation.
    public static bool MoveWhenShown(int x, int y, int w, int h, int timeoutMs) {
        while (Ms() < timeoutMs && !Exited()) {
            IntPtr win = MainWindow();
            if (win != IntPtr.Zero) { SetWindowPos(win, IntPtr.Zero, x, y, w, h, 0x0004 | 0x0010); return true; }
            Thread.Sleep(1);
        }
        return false;
    }
    public static void Post(uint msg, long w, long l) {
        IntPtr h = MainWindow();
        if (h != IntPtr.Zero) PostMessageW(h, msg, new IntPtr(w), new IntPtr(l));
    }

    // ---- capture ---------------------------------------------------------------------------
    static bool Grab(IntPtr h) {
        RECT r; if (!GetWindowRect(h, out r)) return false;
        int w = r.R - r.L, hh = r.B - r.T;
        if (w < 16 || hh < 16) return false;
        if (w != bw || hh != bh || dib == IntPtr.Zero) {
            if (memDC == IntPtr.Zero) memDC = CreateCompatibleDC(IntPtr.Zero);
            BITMAPINFOHEADER bi = new BITMAPINFOHEADER();
            bi.biSize = Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            bi.biWidth = w; bi.biHeight = -hh; bi.biPlanes = 1; bi.biBitCount = 32;
            IntPtr nb; IntPtr nd = CreateDIBSection(IntPtr.Zero, ref bi, 0, out nb, IntPtr.Zero, 0);
            if (nd == IntPtr.Zero) return false;
            SelectObject(memDC, nd);
            if (dib != IntPtr.Zero) DeleteObject(dib);
            dib = nd; bits = nb; bw = w; bh = hh; located = false;
        }
        return PrintWindow(h, memDC, 2);   // PW_RENDERFULLCONTENT: also Direct3D and DirectComposition content
    }
    static int Px(int x, int y) {
        if (x < 0 || y < 0 || x >= bw || y >= bh) return 0;
        return Marshal.ReadInt32(bits, (y * bw + x) * 4);
    }
    // 1..6 for a saturated primary or secondary colour (bits R=4 G=2 B=1), else 0.
    // Ratios, not absolute levels: a square that fades in from black keeps its class.
    static int Cls(int p, out int mx) {
        int b = p & 255, g = (p >> 8) & 255, r = (p >> 16) & 255;
        mx = Math.Max(r, Math.Max(g, b));
        if (mx < 24) return 0;
        int code = 0;
        // "off" reaches up to 45 %: an HDR surface is captured about three times brighter in linear
        // light, which lifts the weak channel of a colour that is not quite pure.
        if (r * 100 >= mx * 72) code |= 4; else if (r * 100 > mx * 45) return 0;
        if (g * 100 >= mx * 72) code |= 2; else if (g * 100 > mx * 45) return 0;
        if (b * 100 >= mx * 72) code |= 1; else if (b * 100 > mx * 45) return 0;
        return code == 7 ? 0 : code;
    }
    // Looks for the square on the vertical centre line of the picture. A window that Windows
    // stretches for another monitor's scale (a player that is not per-monitor DPI aware) is
    // captured unstretched in the top-left part of the bitmap, so smaller extents are tried too.
    static readonly double[] extents = { 1.0, 0.8, 2.0 / 3.0, 4.0 / 7.0, 0.5 };
    static int lastExtent;
    static bool Probe() {
        if (ProbeAt(extents[lastExtent])) return true;
        for (int i = 0; i < extents.Length; i++)
            if (i != lastExtent && ProbeAt(extents[i])) { lastExtent = i; return true; }
        return false;
    }
    static bool ProbeAt(double extent) {
        int ew = (int)(bw * extent), eh = (int)(bh * extent), cx = ew / 2;
        int[] cnt = new int[8]; int[] py = new int[8]; int[] pm = new int[8];
        for (int i = -3; i <= 3; i++) {
            int y = eh / 2 + i * eh / 24, m;
            int c = Cls(Px(cx, y), out m);
            if (c > 0) { cnt[c]++; if (m >= pm[c]) { pm[c] = m; py[c] = y; } }
        }
        int bc = 0;
        for (int c = 1; c < 7; c++) if (cnt[c] >= 2 && cnt[c] > cnt[bc]) bc = c;
        if (bc == 0) return false;
        // edges of the square: left/right at the probe row, then up/down at the middle column
        int y0 = py[bc], m2, xl = cx, xr = cx;
        while (xl > 0 && Cls(Px(xl - 1, y0), out m2) == bc) xl--;
        while (xr < bw - 1 && Cls(Px(xr + 1, y0), out m2) == bc) xr++;
        int mx0 = (xl + xr) / 2, yt = y0, yb = y0;
        while (yt > 0 && Cls(Px(mx0, yt - 1), out m2) == bc) yt--;
        while (yb < bh - 1 && Cls(Px(mx0, yb + 1), out m2) == bc) yb++;
        int w = xr - xl + 1, h = yb - yt + 1;
        if (w < 40 || Math.Abs(w - h) * 12 > w) return false;   // not a square: something else
        sqC = bc; sqMax = pm[bc];
        sqX = mx0; sqY = (yt + yb) / 2; sqS = (w + h) / 2; located = true;
        return true;
    }
    // Frame number from the 12 cells under the square; -1 when the square isn't there.
    static int ReadFrame() {
        if (!located) return -1;
        int thr = Math.Max(20, sqMax / 2), y = sqY + (int)Math.Round(310.0 * sqS / 440.0), f = 0;
        for (int k = 0; k < 12; k++) {
            int x = sqX + (int)Math.Round((-330.0 + 60.0 * k) * sqS / 440.0), m;
            Cls(Px(x, y), out m);
            if (m > thr) f |= 1 << k;
        }
        return f;
    }
    static int GrabFrame(IntPtr h) {
        if (!Grab(h)) return -1;
        return Probe() ? ReadFrame() : -1;
    }
    public static int CurrentFrame() {
        for (int i = 0; i < 5; i++) {
            IntPtr h = MainWindow();
            int f = h == IntPtr.Zero ? -1 : GrabFrame(h);
            if (f >= 0) return f;
            Thread.Sleep(5);
        }
        return -1;
    }
    public static string Square() { return located ? "square " + sqS + "px at " + sqX + "," + sqY + " class " + sqC + " max " + sqMax : "no square"; }
    public static void SavePng(string path) {
        IntPtr h = MainWindow();
        if (h == IntPtr.Zero || !Grab(h)) return;
        using (Bitmap b = new Bitmap(bw, bh, bw * 4, PixelFormat.Format32bppRgb, bits)) b.Save(path, ImageFormat.Png);
    }
    // Mean colour of a block in the middle of the square (for the colour accuracy clip).
    public static string SquareColor() {
        IntPtr h = MainWindow();
        if (h == IntPtr.Zero || !Grab(h) || !located) return "";
        long r = 0, g = 0, b = 0; int n = 0, half = Math.Max(4, sqS / 8);
        for (int y = sqY - half; y <= sqY + half; y += 2)
            for (int x = sqX - half; x <= sqX + half; x += 2) {
                int p = Px(x, y); b += p & 255; g += (p >> 8) & 255; r += (p >> 16) & 255; n++;
            }
        return F((double)r / n) + " " + F((double)g / n) + " " + F((double)b / n);
    }
    public static bool LocateSquare() {
        IntPtr h = MainWindow();
        return h != IntPtr.Zero && Grab(h) && Probe();
    }

    // ---- startup ---------------------------------------------------------------------------
    // After Launch(): when the window showed up, when the first video pixel was seen, when the
    // square reached full brightness, and when the frame number first moved (playback runs).
    // Times are the end of the capture that saw it; "period" is the mean time between captures,
    // so the true moment lies within one period before the reported one.
    public static string MeasureStart(int timeoutMs) {
        double tWin = -1, tFirst = -1, tFull = -1, tAdv = -1, last = -1, sum = 0, worst = 0;
        int f0 = -1, n = 0, w = 0, hh = 0;
        while (Ms() < timeoutMs && !Exited()) {
            IntPtr h = MainWindow();
            if (h == IntPtr.Zero) { Thread.Sleep(1); continue; }
            if (tWin < 0) tWin = Ms();
            if (!Grab(h)) { Thread.Sleep(1); continue; }
            double now = Ms();
            if (last >= 0) { sum += now - last; worst = Math.Max(worst, now - last); n++; }
            last = now; w = bw; hh = bh;
            if (!Probe()) continue;
            if (tFirst < 0) tFirst = now;
            if (tFull < 0 && sqMax >= 225) tFull = now;
            int f = ReadFrame();
            if (f0 < 0) f0 = f; else if (f != f0 && tAdv < 0) tAdv = now;
            if (tFull >= 0 && tAdv >= 0) break;
        }
        return "window=" + F(tWin) + " first=" + F(tFirst) + " full=" + F(tFull) + " moving=" + F(tAdv) +
               " frame0=" + f0 + " period=" + F(n > 0 ? sum / n : -1) + " worst=" + F(worst) + " size=" + w + "x" + hh;
    }
    // ms after Launch() until the square is on screen at full brightness (clips without frame numbers).
    public static double WaitSquare(int timeoutMs) {
        while (Ms() < timeoutMs && !Exited()) {
            IntPtr h = MainWindow();
            if (h == IntPtr.Zero || !Grab(h)) { Thread.Sleep(1); continue; }
            if (Probe() && sqMax >= 225) return Ms();
        }
        return -1;
    }
    // Window only (no file): ms until the main window exists.
    public static double MeasureWindow(int timeoutMs) {
        while (Ms() < timeoutMs && !Exited()) {
            if (MainWindow() != IntPtr.Zero) return Ms();
            Thread.Sleep(1);
        }
        return -1;
    }

    // ---- seek ------------------------------------------------------------------------------
    public static void Mark() { tMark = Stopwatch.GetTimestamp(); }
    // ms from Mark() until a capture shows a frame other than `from`; the new frame in `to`.
    public static double WaitFrameChange(int from, int timeoutMs, out int to) {
        to = -1;
        while (Since(tMark) < timeoutMs && !Exited()) {
            IntPtr h = MainWindow();
            if (h == IntPtr.Zero) { Thread.Sleep(1); continue; }
            int f = GrabFrame(h);
            if (f >= 0 && f != from) { to = f; return Since(tMark); }
        }
        return -1;
    }
    // The same during playback: frames move on by themselves, so wait for one that is at least
    // `minJump` frames ahead of `from` (or behind it).
    public static double WaitFrameJump(int from, int minJump, int timeoutMs, out int to) {
        to = -1;
        while (Since(tMark) < timeoutMs && !Exited()) {
            IntPtr h = MainWindow();
            if (h == IntPtr.Zero) { Thread.Sleep(1); continue; }
            int f = GrabFrame(h);
            if (f >= 0 && (f >= from + minJump || f < from)) { to = f; return Since(tMark); }
        }
        return -1;
    }
    public static double PostAndWaitJump(uint msg, long wp, long lp, int from, int minJump, int timeoutMs, out int to) {
        Mark(); Post(msg, wp, lp);
        return WaitFrameJump(from, minJump, timeoutMs, out to);
    }
    public static double RcSendAndWaitJump(string line, int from, int minJump, int timeoutMs, out int to) {
        Mark(); RcSend(line);
        return WaitFrameJump(from, minJump, timeoutMs, out to);
    }
    public static double PostAndWait(uint msg, long wp, long lp, int from, int timeoutMs, out int to) {
        Mark(); Post(msg, wp, lp);
        return WaitFrameChange(from, timeoutMs, out to);
    }
    // VLC's remote-control interface (--extraintf rc --rc-host 127.0.0.1:port).
    static TcpClient rc;
    public static bool RcConnect(int port, int timeoutMs) {
        long from = Stopwatch.GetTimestamp();
        while (Since(from) < timeoutMs) {
            try { rc = new TcpClient(); rc.NoDelay = true; rc.Connect("127.0.0.1", port); return true; }
            catch (Exception) { Thread.Sleep(50); }
        }
        return false;
    }
    public static void RcSend(string line) {
        byte[] b = Encoding.ASCII.GetBytes(line + "\n");
        rc.GetStream().Write(b, 0, b.Length); rc.GetStream().Flush();
    }
    public static double RcSendAndWait(string line, int from, int timeoutMs, out int to) {
        Mark(); RcSend(line);
        return WaitFrameChange(from, timeoutMs, out to);
    }
    public static void RcClose() { try { if (rc != null) rc.Close(); } catch (Exception) { } rc = null; }

    // ---- smoothness ------------------------------------------------------------------------
    // Watches the frame numbers for `seconds`: how many of the frames that should have been
    // shown were seen at least once, and the longest time one frame stayed on screen.
    public static string Smoothness(double seconds, double fps) {
        IntPtr h = MainWindow();
        if (h == IntPtr.Zero) return "no window";
        HashSet<int> seen = new HashSet<int>();
        long from = Stopwatch.GetTimestamp();
        int first = -1, lastF = -1, n = 0; double lastChange = 0, longest = 0;
        while (Since(from) < seconds * 1000) {
            int f = GrabFrame(h); double now = Since(from); n++;
            if (f < 0) continue;
            if (first < 0) { first = f; lastChange = now; }
            if (f != lastF) { if (lastF >= 0) longest = Math.Max(longest, now - lastChange); lastChange = now; lastF = f; }
            seen.Add(f);
        }
        int span = lastF - first + 1;
        double wall = Since(from) / 1000.0;
        return "frames=" + span + " seen=" + seen.Count + " rate=" + (span / wall / fps).ToString("0.000", Inv) +
               " longest=" + F(longest) + " captures_per_s=" + F(n / wall);
    }

    // ---- load ------------------------------------------------------------------------------
    // CPU (percent of one core), memory and GPU engines of the player process over `seconds`.
    public static string Load(double seconds) {
        Process p = Process.GetProcessById(Pid);
        Dictionary<string, PerformanceCounter> gpu = new Dictionary<string, PerformanceCounter>();
        Dictionary<string, double> sums = new Dictionary<string, double>();
        string prefix = "pid_" + Pid + "_";
        try {
            foreach (string name in new PerformanceCounterCategory("GPU Engine").GetInstanceNames())
                if (name.StartsWith(prefix)) {
                    PerformanceCounter c = new PerformanceCounter("GPU Engine", "Utilization Percentage", name, true);
                    c.NextValue(); gpu[name] = c;
                }
        } catch (Exception) { }
        TimeSpan cpu0 = p.TotalProcessorTime; long from = Stopwatch.GetTimestamp();
        int ticks = 0; long privPeak = 0, wsPeak = 0; double privSum = 0, wsSum = 0;
        while (Since(from) < seconds * 1000) {
            Thread.Sleep(1000);
            p.Refresh(); ticks++;
            privSum += p.PrivateMemorySize64; wsSum += p.WorkingSet64;
            privPeak = Math.Max(privPeak, p.PrivateMemorySize64); wsPeak = Math.Max(wsPeak, p.WorkingSet64);
            foreach (KeyValuePair<string, PerformanceCounter> kv in gpu) {
                int at = kv.Key.IndexOf("engtype_");
                string type = at >= 0 ? kv.Key.Substring(at + 8) : "other";
                double v; try { v = kv.Value.NextValue(); } catch (Exception) { v = 0; }
                double s; sums.TryGetValue(type, out s); sums[type] = s + v;
            }
        }
        p.Refresh();
        double wall = Since(from) / 1000.0, cpuPct = (p.TotalProcessorTime - cpu0).TotalSeconds / wall * 100.0;
        double gpuMem = 0;
        try {
            foreach (string name in new PerformanceCounterCategory("GPU Process Memory").GetInstanceNames())
                if (name.StartsWith(prefix))
                    using (PerformanceCounter c = new PerformanceCounter("GPU Process Memory", "Dedicated Usage", name, true)) gpuMem += c.RawValue;
        } catch (Exception) { }
        StringBuilder sb = new StringBuilder();
        sb.Append("cpu=" + F(cpuPct) + " private=" + F(privSum / ticks / 1048576.0) + " ws=" + F(wsSum / ticks / 1048576.0) +
                  " private_peak=" + F(privPeak / 1048576.0) + " gpu_mem=" + F(gpuMem / 1048576.0) + " threads=" + p.Threads.Count +
                  " handles=" + p.HandleCount);
        foreach (KeyValuePair<string, double> kv in sums) if (kv.Value / ticks >= 0.05) sb.Append(" gpu_" + kv.Key + "=" + F(kv.Value / ticks));
        foreach (PerformanceCounter c in gpu.Values) c.Dispose();
        return sb.ToString();
    }
}
