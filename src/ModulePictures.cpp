// Module pictures for PATCH GEN (liquidnick.com/patchgen): the page shows each module of a generated patch as it looks
// in Rack, from Rack's own screenshots in <Rack user folder>/screenshots/<plugin>/<module>.png. Rack only makes those when
// started with "-t", only one Rack may run at a time, and a web page can't start programs, so this menu item does it:
//   1. asks first, then starts a small helper script and closes Rack the normal way (the autosave keeps the patch);
//   2. the helper waits for Rack to exit, then makes a private Rack user folder (<user folder>-pictures) whose plugins
//      folder is a link to the real one (a directory junction on Windows, a symlink elsewhere), so the real settings,
//      autosave and log.txt are never touched;
//   3. runs Rack minimised on that folder in screenshot mode ("-u <folder> -t 1"): it pictures every installed module
//      and quits by itself;
//   4. copies the pictures into the real screenshots folder, removes the link on its own (never followed), then the
//      private folder;
//   5. reopens Rack (with the patch) and PATCH GEN in the browser, and leaves the count in <user folder>-pictures.txt
//      for this menu to show.
#include "ModulePictures.hpp"
// It closes Rack and starts programs, so it is only in development builds (make with SYNTHBLOK_DEV=1, as
// build_and_install.bat does). Release builds (make dist, GitHub Actions, VCV Library) leave the menu item out.
#ifndef SYNTHBLOK_DEV
namespace ModulePictures {
void appendMenu(rack::ui::Menu*) {}
}
#else
#include <osdialog.h>
#include <fstream>
#if defined ARCH_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#if defined ARCH_MAC
#include <mach-o/dyld.h>
#endif

using namespace rack;

namespace ModulePictures {
namespace {

const char* URL = "https://liquidnick.com/patchgen/";

// the running Rack's own executable
std::string rackExecutable() {
#if defined ARCH_WIN
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH * 4);
    return n ? string::UTF16toUTF8(std::wstring(buf, n)) : "";
#elif defined ARCH_MAC
    char buf[4096]; uint32_t size = sizeof(buf);
    return _NSGetExecutablePath(buf, &size) == 0 ? std::string(buf) : "";
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? std::string(buf, n) : "";
#endif
}

std::string resultPath() { return asset::userDir + "-pictures.txt"; }

#if defined ARCH_WIN
std::string winPath(std::string p) { for (char& c : p) if (c == '/') c = '\\'; return p; }
std::string psq(const std::string& s) {   // a PowerShell single-quoted string
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "''"; else o += c; }
    return o + "'";
}

std::string helperScript(int pid, const std::string& exe) {
    return std::string("\xEF\xBB\xBF")   // UTF-8 BOM: Windows PowerShell then reads non-ASCII folder names right
        + "# SynTHBloK: module pictures for PATCH GEN (written by the plugin; safe to delete)\n"
        + "$ErrorActionPreference = 'SilentlyContinue'\n"
        + "$rackPid = " + std::to_string(pid) + "\n"
        + "$exe = " + psq(winPath(exe)) + "\n"
        + "$plugins = " + psq(winPath(plugin::pluginsPath)) + "\n"
        + "$priv = " + psq(winPath(asset::userDir + "-pictures")) + "\n"
        + "$shots = " + psq(winPath(asset::user("screenshots"))) + "\n"
        + "$result = " + psq(winPath(resultPath())) + "\n"
        + "$url = " + psq(URL) + "\n"
        + R"PS(
Wait-Process -Id $rackPid -Timeout 180
if (Get-Process -Id $rackPid) { exit 1 }   # Rack did not close: change nothing
# links are removed on their own (rmdir on a junction never touches what it points to), and the private
# folder is deleted only when none are left
function Drop-Links { Get-ChildItem -LiteralPath $priv -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | ForEach-Object { cmd /c rmdir "$($_.FullName)" } }
function Clear-Private {
  if (-not (Test-Path -LiteralPath $priv)) { return }
  Drop-Links
  if (@(Get-ChildItem -LiteralPath $priv -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -eq 0) { cmd /c rd /s /q "\\?\$priv" }
}
$n = 0
Clear-Private
New-Item -ItemType Directory -Force $priv | Out-Null
$link = Join-Path $priv (Split-Path $plugins -Leaf)
cmd /c mklink /J "$link" "$plugins" | Out-Null
if (Test-Path -LiteralPath $link) {
  $p = Start-Process -FilePath $exe -ArgumentList '-u', "`"$priv`"", '-t', '1' -WorkingDirectory (Split-Path $exe) -WindowStyle Minimized -PassThru
  if (-not $p.WaitForExit(1800000)) { Stop-Process -Id $p.Id -Force }
  cmd /c rmdir "$link"
  foreach ($d in @(Get-ChildItem -LiteralPath (Join-Path $priv 'screenshots') -Directory)) {
    $out = Join-Path $shots $d.Name
    New-Item -ItemType Directory -Force $out | Out-Null
    foreach ($f in @(Get-ChildItem -LiteralPath $d.FullName -Filter *.png)) {
      Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $out $f.Name) -Force
      if ($?) { $n++ }
    }
  }
  Clear-Private
}
Set-Content -LiteralPath $result -Value $n
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
if ($n -gt 0) { Start-Process $url }
Remove-Item -LiteralPath $PSCommandPath
)PS";
}

bool startHelper(const std::string& script) {
    const std::string path = system::join(system::getTempDirectory(), "SynTHBloK-pictures.ps1");
    { std::ofstream f(string::UTF8toUTF16(path).c_str(), std::ios::binary); if (!f) return false; f << script; }
    wchar_t sysdir[MAX_PATH];
    GetSystemDirectoryW(sysdir, MAX_PATH);
    std::string ps = string::UTF16toUTF8(sysdir) + "\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = string::UTF8toUTF16("\"" + ps + "\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + winPath(path) + "\"");
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    // its own process: it keeps running after Rack closes
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
int processId() { return (int) GetCurrentProcessId(); }
#else
std::string shq(const std::string& s) {   // a shell single-quoted string
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

std::string helperScript(int pid, const std::string& exe) {
    return std::string("#!/bin/sh\n# SynTHBloK: module pictures for PATCH GEN (written by the plugin; safe to delete)\n")
        + "rackpid=" + std::to_string(pid) + "\n"
        + "exe=" + shq(exe) + "\n"
        + "plugins=" + shq(plugin::pluginsPath) + "\n"
        + "priv=" + shq(asset::userDir + "-pictures") + "\n"
        + "shots=" + shq(asset::user("screenshots")) + "\n"
        + "result=" + shq(resultPath()) + "\n"
        + "url=" + shq(URL) + "\n"
#if defined ARCH_MAC
        + "opener=open\n"
#else
        + "opener=xdg-open\n"
#endif
        + R"SH(
i=0; while kill -0 "$rackpid" 2>/dev/null; do sleep 1; i=$((i+1)); [ $i -gt 180 ] && exit 1; done
# links are removed on their own (rm on a symlink never touches what it points to), and the private folder
# is deleted only when none are left (rm -rf does not follow symlinks either)
clear_private() {
  [ -d "$priv" ] || return 0
  find "$priv" -maxdepth 1 -type l -exec rm {} \;
  [ -z "$(find "$priv" -type l)" ] && rm -rf "$priv"
}
n=0
clear_private
mkdir -p "$priv"
link="$priv/$(basename "$plugins")"
if ln -s "$plugins" "$link"; then
  "$exe" -u "$priv" -t 1 >/dev/null 2>&1 &
  rp=$!; j=0
  while kill -0 "$rp" 2>/dev/null; do sleep 1; j=$((j+1)); [ $j -gt 1800 ] && kill -9 "$rp"; done
  rm "$link"
  for d in "$priv/screenshots"/*/; do
    [ -d "$d" ] || continue
    out="$shots/$(basename "$d")"; mkdir -p "$out"
    for f in "$d"*.png; do [ -f "$f" ] && cp "$f" "$out/" && n=$((n+1)); done
  done
  clear_private
fi
echo "$n" > "$result"
nohup "$exe" >/dev/null 2>&1 &
[ "$n" -gt 0 ] && "$opener" "$url" >/dev/null 2>&1
rm -f "$0"
)SH";
}

bool startHelper(const std::string& script) {
    const std::string path = system::join(system::getTempDirectory(), "SynTHBloK-pictures.sh");
    { std::ofstream f(path, std::ios::binary); if (!f) return false; f << script; }
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();   // its own session: it keeps running after Rack closes
        execl("/bin/sh", "sh", path.c_str(), (char*) NULL);
        _exit(127);
    }
    return true;
}
int processId() { return (int) getpid(); }
#endif

void makePictures() {
    const std::string exe = rackExecutable();
    if (exe.empty() || !system::isFile(exe) || !system::isDirectory(plugin::pluginsPath)) {
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "SynTHBloK can't find Rack's program or plugins folder, so it can't make module pictures here.");
        return;
    }
    if (!osdialog_message(OSDIALOG_INFO, OSDIALOG_OK_CANCEL,
            "Make module pictures for PATCH GEN?\n\n"
            "Rack will close (your patch is kept), take a picture of every module you have "
            "(a few minutes, in the background), then open again with your patch. "
            "PATCH GEN then opens in your browser.\n\n"
            "Your settings and patch files are not changed."))
        return;
    system::remove(resultPath());
    if (!startHelper(helperScript(processId(), exe))) {
        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "SynTHBloK couldn't start the picture helper. Nothing was changed.");
        return;
    }
    APP->window->close();   // a normal close: Rack saves the autosave as it quits
}

}  // namespace

void appendMenu(ui::Menu* menu) {
    menu->addChild(new MenuSeparator);
    // the last run's count, left by the helper
    int last = -1;
    try { std::ifstream f(resultPath()); if (f) f >> last; } catch (...) {}
    if (last > 0)
        menu->addChild(createMenuItem(string::f("Module pictures ready (%d): open PATCH GEN", last), "",
                                      []() { system::openBrowser(URL); }));
    else if (last == 0)
        menu->addChild(createMenuLabel("Last module pictures run made no pictures"));
    menu->addChild(createMenuItem(last > 0 ? "Make module pictures again (restarts Rack)" : "Make module pictures for PATCH GEN (restarts Rack)",
                                  "", []() { makePictures(); }));
}

}  // namespace ModulePictures
#endif  // SYNTHBLOK_DEV
