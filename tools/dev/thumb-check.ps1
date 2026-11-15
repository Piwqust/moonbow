# Renders Explorer thumbnails through MoonbowThumbs.dll without registering it: loads the DLL,
# asks it for each file's thumbnail and saves <Out>\<name>.png. Fails when any file gets none.
# Usage: powershell -File tools\dev\thumb-check.ps1 -Dll build\MoonbowThumbs.dll -Out $env:TEMP\thumbs a.mp3 b.aac
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory)][string]$Dll,
    [Parameter(Mandatory)][string]$Out,
    [int]$Size = 256,
    [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files
)
$ErrorActionPreference = "Stop"
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

[ComImport, Guid("00000001-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IClassFactory {
    [PreserveSig] int CreateInstance(IntPtr outer, ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object obj);
    [PreserveSig] int LockServer(bool lockIt);
}
[ComImport, Guid("b824b49d-22ac-4161-ac8a-9916e8fa3f7f"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IInitializeWithStream { [PreserveSig] int Initialize(IStream stream, uint mode); }
[ComImport, Guid("e357fccd-a995-4576-b01f-234630154e96"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IThumbnailProvider { [PreserveSig] int GetThumbnail(uint cx, out IntPtr bmp, out int alpha); }

public static class ThumbCheck {
    [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr LoadLibraryW(string path);
    [DllImport("kernel32", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr m, string name);
    [DllImport("shlwapi", CharSet = CharSet.Unicode)] static extern int SHCreateStreamOnFileEx(string file, uint mode, uint attr, bool create, IntPtr tmpl, out IStream s);
    [DllImport("gdi32")] static extern bool DeleteObject(IntPtr o);
    delegate int GetClassObject(ref Guid clsid, ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object obj);

    public static string Render(string dll, string file, string png, uint cx) {
        IntPtr m = LoadLibraryW(dll);
        if (m == IntPtr.Zero) throw new Exception("cannot load " + dll);
        var get = (GetClassObject)Marshal.GetDelegateForFunctionPointer(GetProcAddress(m, "DllGetClassObject"), typeof(GetClassObject));
        Guid clsid = new Guid("6DBA9BBC-C992-4C5D-ACCB-EE3578FFB188"), iidFactory = typeof(IClassFactory).GUID, iidUnk = new Guid("00000000-0000-0000-C000-000000000046");
        object f;
        Marshal.ThrowExceptionForHR(get(ref clsid, ref iidFactory, out f));
        object o;
        Marshal.ThrowExceptionForHR(((IClassFactory)f).CreateInstance(IntPtr.Zero, ref iidUnk, out o));
        IStream s;
        Marshal.ThrowExceptionForHR(SHCreateStreamOnFileEx(file, 0x20 /* STGM_READ|SHARE_DENY_WRITE */, 0, false, IntPtr.Zero, out s));
        Marshal.ThrowExceptionForHR(((IInitializeWithStream)o).Initialize(s, 0));
        IntPtr bmp; int alpha;
        int hr = ((IThumbnailProvider)o).GetThumbnail(cx, out bmp, out alpha);
        if (hr != 0 || bmp == IntPtr.Zero) return "no thumbnail, hr=0x" + hr.ToString("X8");
        using (var img = System.Drawing.Image.FromHbitmap(bmp)) {
            img.Save(png, System.Drawing.Imaging.ImageFormat.Png);
            DeleteObject(bmp);
            return img.Width + "x" + img.Height;
        }
    }
}
'@
New-Item -ItemType Directory -Force $Out | Out-Null
$dllPath = (Resolve-Path $Dll).Path
$failed = 0
foreach ($f in $Files) {
    $path = (Resolve-Path -LiteralPath $f).Path
    $png = Join-Path $Out ([IO.Path]::GetFileNameWithoutExtension($path) + ".png")
    $result = [ThumbCheck]::Render($dllPath, $path, $png, $Size)
    if ($result -like "no thumbnail*") { $failed++ }
    Write-Host "$result  $([IO.Path]::GetFileName($path))"
}
if ($failed) { throw "$failed file(s) got no thumbnail" }
