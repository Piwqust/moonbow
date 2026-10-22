// A process's handles as "Type|name" and its threads as "Thread start|module+offset",
// so two snapshots tell what a leak is and whose (soak.ps1 diffs them in report.txt).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class HandleTypes {
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int ret);
    [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h, int cls, IntPtr buf, int len, out int ret);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool DuplicateHandle(IntPtr sp, IntPtr s, IntPtr dp, out IntPtr d, int access, bool inherit, int opts);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern int GetFinalPathNameByHandleW(IntPtr h, StringBuilder sb, int n, int flags);
    [DllImport("kernel32.dll")] static extern int GetFileType(IntPtr h);

    static string UStr(IntPtr buf) {
        int len = Marshal.ReadInt16(buf);
        IntPtr p = Marshal.ReadIntPtr(buf, 8);
        return len > 0 && p != IntPtr.Zero ? Marshal.PtrToStringUni(p, len / 2) : "";
    }

    static string Query(IntPtr h, int cls) {
        IntPtr b = Marshal.AllocHGlobal(4096);
        try {
            int ret;
            return NtQueryObject(h, cls, b, 4096, out ret) == 0 ? UStr(b) : null;
        } finally { Marshal.FreeHGlobal(b); }
    }

    [DllImport("ntdll.dll")] static extern int NtQueryInformationThread(IntPtr h, int cls, out IntPtr info, int len, IntPtr ret);
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int tid);

    // "Thread start|module+offset" of each thread's start address.
    public static List<string> ThreadStarts(int pid) {
        var result = new List<string>();
        var p = System.Diagnostics.Process.GetProcessById(pid);
        var mods = new List<System.Diagnostics.ProcessModule>();
        foreach (System.Diagnostics.ProcessModule m in p.Modules) mods.Add(m);
        foreach (System.Diagnostics.ProcessThread t in p.Threads) {
            IntPtr h = OpenThread(0x40 /* THREAD_QUERY_INFORMATION */, false, t.Id);
            if (h == IntPtr.Zero) continue;
            IntPtr start;
            string where = "?";
            if (NtQueryInformationThread(h, 9, out start, IntPtr.Size, IntPtr.Zero) == 0) {
                long a = start.ToInt64();
                where = "0x" + a.ToString("X");
                foreach (var m in mods) {
                    long b = m.BaseAddress.ToInt64();
                    if (a >= b && a < b + m.ModuleMemorySize) { where = m.ModuleName + "+0x" + (a - b).ToString("X"); break; }
                }
            }
            CloseHandle(h);
            result.Add("Thread start|" + where);
        }
        return result;
    }

    // "Type|name" for every handle of pid. Names only where asking can't hang (not pipes).
    public static List<string> Snap(int pid) {
        var result = new List<string>();
        int len = 1 << 20, ret;
        IntPtr buf;
        while (true) {
            buf = Marshal.AllocHGlobal(len);
            int st = NtQuerySystemInformation(64, buf, len, out ret);
            if (st == 0) break;
            Marshal.FreeHGlobal(buf);
            if (st != unchecked((int)0xC0000004)) throw new Exception("NtQuerySystemInformation " + st.ToString("X"));
            len *= 2;
        }
        IntPtr proc = OpenProcess(0x40 /* PROCESS_DUP_HANDLE */, false, pid);
        try {
            long n = Marshal.ReadInt64(buf);
            for (long i = 0; i < n; i++) {
                IntPtr e = new IntPtr(buf.ToInt64() + 16 + i * 40);
                if (Marshal.ReadInt64(e, 8) != pid) continue;
                IntPtr value = Marshal.ReadIntPtr(e, 16);
                int typeIndex = (ushort)Marshal.ReadInt16(e, 30);
                string type = "type#" + typeIndex, name = "";
                IntPtr dup;
                if (proc != IntPtr.Zero && DuplicateHandle(proc, value, GetCurrentProcess(), out dup, 0, false, 2)) {
                    type = Query(dup, 2) ?? type;
                    if (type == "File") {
                        if (GetFileType(dup) == 1) {
                            var sb = new StringBuilder(1024);
                            name = GetFinalPathNameByHandleW(dup, sb, 1024, 0) > 0 ? sb.ToString() : "(disk)";
                        } else name = "(pipe/char)";
                    } else {
                        name = Query(dup, 1) ?? "";
                    }
                    CloseHandle(dup);
                }
                result.Add(type + "|" + name);
            }
        } finally {
            if (proc != IntPtr.Zero) CloseHandle(proc);
            Marshal.FreeHGlobal(buf);
        }
        return result;
    }
}
