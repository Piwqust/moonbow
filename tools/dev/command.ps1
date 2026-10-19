# Sends a test command to a Lumen --automation instance and prints "ok" or "failed" plus the
# app's reply. Exit code 0 = ok, 1 = failed. Only --automation runs accept commands.
# Usage:
#   tools\dev\command.ps1 -ProcessId 1234 -Command "nav music songs"
#   tools\dev\command.ps1 -ProcessId 1234 -Command "wait-idle" [-TimeoutMs 10000]
#   add -Quiet to print nothing (use the exit code).
#
# Transport: WM_COPYDATA to the main LumenHost window (the unowned one), dwData = 0x4C554D,
# lpData = the command as UTF-16. SendMessageTimeout returns TRUE on success; the reply text is
# in %TEMP%\lumen-cmd-<pid>.txt ("ok"/"failed" on the first line, then details).
# Commands run synchronously on the UI thread: the state has changed when this returns, but the
# next layout/frame (and anything asynchronous, like loading a file) happens afterwards, so
# follow a command with "wait-idle" before a capture or a dump.
#
# COMMANDS
#   action <id>           run an action (same as its shortcut); fails when unknown or disabled
#                         (e.g. playback.* without a file). Ids:
#     playback.play_pause playback.stop playback.previous playback.next
#     playback.seek_forward playback.seek_backward playback.seek_forward_long
#     playback.seek_backward_long playback.seek_forward_short playback.seek_backward_short
#     playback.seek_forward_xl playback.seek_backward_xl playback.frame_step playback.frame_back
#     playback.speed_up playback.speed_down playback.speed_reset playback.next_chapter
#     playback.prev_chapter playback.replay playback.repeat playback.shuffle playback.ab_loop
#     playback.jump_0 playback.jump_10 ... playback.jump_90
#     audio.volume_up audio.volume_down audio.mute audio.cycle audio.delay_up audio.delay_down
#     subtitles.cycle subtitles.toggle subtitles.delay_up subtitles.delay_down subtitles.bigger
#     subtitles.smaller
#     video.fullscreen video.exit_fullscreen video.hide_ui video.pip video.snapshot video.record
#     video.zoom_in video.zoom_out video.zoom_reset video.aspect video.rotate video.deinterlace
#     video.statistics
#     media.open_file media.open_folder media.open_network media.open_subtitles media.close
#     media.home media.sidebar media.playlist media.search media.back media.forward
#     media.bookmark media.favorite media.info media.show_in_explorer
#     app.settings app.shortcuts app.customize app.quit
#     (media.open_file/open_folder/open_subtitles/show_in_explorer do nothing visible in
#     --automation: native dialogs and Explorer are blocked.)
#   nav <route> [path]    show the library at a route (leaves the player screen):
#     nav home
#     nav videos [movies|shows|folders|show:<show name, lower case>|<folder path>]
#     nav music [songs|albums|artists|genres|album:<name>|artist:<name>|genre:<name>]
#     nav network [<sub-page>]
#     nav playlists [<playlist id>]
#     nav recent [continue|audio|favorites]
#     nav back | nav forward         library history (fails when there is none)
#     nav player | nav library       switch screens without navigating
#     nav settings [<page>]          open (or switch) the Settings window; pages:
#       general interface privacy controls playback video audio subtitles library network
#       performance recording extensions
#   panel <name>          open the player's side panel on a tab (switches to the player screen):
#     tracks (= audio) subtitles settings queue chapters bookmarks speed; panel close
#   open <path|url>       play a file, folder, playlist or URL (no dialog); fails if missing
#   queue <path|url>      append to the queue without playing
#   drop <path>           as if the file or folder was dropped on the main window
#   set <key> <value>     change a setting live, as Settings does (Config::Set + listeners).
#                         value is JSON when it parses (true, 0.5, "x"), else a plain string:
#                         set general.theme light | set interface.autoHide always
#                         set interface.decorations false | set general.language en
#   menu <name>           open a menu without a click:
#     app (the Lumen logo menu in the title bar), more / sort (library header), add (sidebar "Add"),
#     player (video right-click menu; player screen), pip (PiP menu; PiP only), close
#   click <selector>      OnClick() on the first visible match (topmost popup first, then the
#                         window), walking up to an interactive ancestor:
#     #<id> | icon=<name> | type=<part of C++ type name> | tip=<tooltip text>
#   wait-idle             ok when every window has nothing to lay out, draw or run, no
#                         animation runs, mpv is not starting or loading, no library scan and
#                         no thumbnail jobs. This script polls it until ok or -TimeoutMs.
#                         (Timers such as the 2.2 s control-bar auto-hide don't count.)
#   dump [all]            write the tree dump of the main window (or every visible window);
#                         the reply lists the files. Same as tools\dev\dump-tree.ps1.
#   state                 reply: route, player screen, file loaded, paused, queue size,
#                         current index, fullscreen, pip, open popups (key=value lines)
#   prop <name>           reply: <name>=<value>, an mpv property read as a string
#                         (prop hwdec-current, prop vf, prop frame-drop-count); fails until
#                         the player has started
param([Parameter(Mandatory = $true)][int]$ProcessId, [Parameter(Mandatory = $true)][string]$Command,
      [int]$TimeoutMs = 10000, [switch]$Quiet)
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LumenCmd {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct COPYDATASTRUCT { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", EntryPoint="SendMessageTimeoutW")]
  public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, ref COPYDATASTRUCT l, uint flags, uint ms, out IntPtr result);
  public static IntPtr FindMain(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && c.ToString() == "LumenHost" && GetWindow(h, 4) == IntPtr.Zero) { found = h; return false; }
      return true; }, IntPtr.Zero);
    return found;
  }
  // 1 = ok, 0 = failed, -1 = no answer (timeout / hung).
  public static int Send(IntPtr h, string text, uint ms) {
    byte[] bytes = Encoding.Unicode.GetBytes(text + "\0");
    IntPtr mem = Marshal.AllocHGlobal(bytes.Length);
    try {
      Marshal.Copy(bytes, 0, mem, bytes.Length);
      var cds = new COPYDATASTRUCT { dwData = (IntPtr)0x4C554D, cbData = bytes.Length, lpData = mem };
      IntPtr result;
      if (SendMessageTimeout(h, 0x004A, IntPtr.Zero, ref cds, 2, ms, out result) == IntPtr.Zero) return -1;
      return result != IntPtr.Zero ? 1 : 0;
    } finally { Marshal.FreeHGlobal(mem); }
  }
}
"@
$h = [LumenCmd]::FindMain([uint32]$ProcessId)
if ($h -eq [IntPtr]::Zero) { throw "no main LumenHost window for process $ProcessId" }
# Only --automation instances (WS_EX_NOACTIVATE windows, ui/Host.cpp) take commands; say so instead of waiting.
if (([LumenCmd]::GetWindowLong($h, -20) -band 0x08000000) -eq 0) { throw "process $ProcessId is not a --automation instance" }
$replyFile = Join-Path $env:TEMP "lumen-cmd-$ProcessId.txt"
$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
while ($true) {
  $res = [LumenCmd]::Send($h, $Command, [uint32][Math]::Max(1000, $TimeoutMs))
  if ($res -ne 0 -or $Command.Trim() -ne "wait-idle" -or [DateTime]::UtcNow -ge $deadline) { break }
  Start-Sleep -Milliseconds 25
}
$reply = ""
if ($res -ge 0 -and (Test-Path $replyFile)) {
  $reply = [IO.File]::ReadAllText($replyFile, [Text.Encoding]::UTF8)
  $nl = $reply.IndexOf("`n"); $reply = if ($nl -ge 0) { $reply.Substring($nl + 1).TrimEnd() } else { "" }
}
if (-not $Quiet) {
  if ($res -eq 1) { "ok" } elseif ($res -eq 0) { "failed" } else { "failed (no answer)" }
  if ($reply) { $reply }
}
if ($res -eq 1) { exit 0 } else { exit 1 }
