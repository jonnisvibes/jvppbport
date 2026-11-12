// JVPPBPort: things that differ between Linux, Windows and Android: where
// the save, settings and snapshots go, where messages go, and crash reports.
//
// Data folder (the save, the settings file, screenshots, F12 snapshots):
//   Windows: %APPDATA%\JVPPBPort\  (C:\Users\<name>\AppData\Roaming\JVPPBPort;
//            a JVPPBPort folder from before the rename is moved there)
//   Linux:   build-JVPPBPort/      (the development build, run from the repo root)
//   Android: the app's own folder in the phone's shared storage,
//            Android/data/com.jvppbport/files/ (seen from a PC over USB, so
//            the log, save and screenshots can be copied off; removed with the
//            app); the app's private folder if that isn't there
//   JVPPBPORT_DATA_DIR=folder      overrides all
// Asset folder (gamedata.bin and tables.bin, made from the player's ROM; see
// gamedata.c): the data folder, or JVPPBPORT_ASSET_DIR=folder.
// Messages: on Windows the program has no console window, so they go to
// jvppbport.log in the data folder (JVPPBPORT_CONSOLE=1 opens a console
// instead). Android does the same (there's no terminal either). On Linux
// they go to the terminal as before.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#elif defined(__ANDROID__)
#include <dlfcn.h>
#include <unwind.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL.h>
#else
#include <execinfo.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "jvppbport.h"

static char sDataDir[1024];
static char sAssetDir[1024];

// Set in a copy of the program made with fork() (Android's table pictures,
// gamedata.c): it must leave with _exit, not exit, so it doesn't run the
// app's own shutdown code.
int gPortForkedCopy;

void PortExit(int code)
{
    fflush(NULL);
    if (gPortForkedCopy)
        _exit(code);
    exit(code);
}

// Adds a / at the end of a folder name if it has none.
static void EndWithSlash(char *dir, size_t size)
{
    size_t len = strlen(dir);

    if (len && dir[len - 1] != '/' && dir[len - 1] != '\\' && len + 1 < size)
        strcat(dir, "/");
}

// Builds before the public name changed wrote jcppbport.sav/.ini/.log: give
// the save and settings the new names once (unless new ones exist already)
// and drop the old log.
static void RenameOldFiles(void)
{
    static const char *const names[] = { "sav", "ini" };
    char from[1100], to[1100];
    FILE *f;
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        snprintf(from, sizeof(from), "%sjcppbport.%s", sDataDir, names[i]);
        snprintf(to, sizeof(to), "%sjvppbport.%s", sDataDir, names[i]);
        if ((f = fopen(to, "rb")) != NULL)
        {
            fclose(f);
            continue;
        }
        rename(from, to);
    }
    snprintf(from, sizeof(from), "%sjcppbport.log", sDataDir);
    remove(from);
}

static void FindDataDir(void)
{
    const char *dir = getenv("JVPPBPORT_DATA_DIR");

    if (dir && *dir)
        snprintf(sDataDir, sizeof(sDataDir), "%s", dir);
#ifdef _WIN32
    else if (getenv("APPDATA"))
    {
        char old[1024];

        // Builds before the public name changed used %APPDATA%\JCPPBPort:
        // move it to the new name once, so saves and settings come along. If
        // it can't be moved (a file in it open elsewhere), keep using it.
        snprintf(sDataDir, sizeof(sDataDir), "%s\\JVPPBPort", getenv("APPDATA"));
        snprintf(old, sizeof(old), "%s\\JCPPBPort", getenv("APPDATA"));
        if (GetFileAttributesA(sDataDir) == INVALID_FILE_ATTRIBUTES
            && GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES
            && !MoveFileA(old, sDataDir))
            snprintf(sDataDir, sizeof(sDataDir), "%s", old);
        _mkdir(sDataDir);
    }
#elif defined(__ANDROID__)
    else if ((SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)
             && SDL_AndroidGetExternalStoragePath())
        snprintf(sDataDir, sizeof(sDataDir), "%s", SDL_AndroidGetExternalStoragePath());
    else if (SDL_AndroidGetInternalStoragePath())
        snprintf(sDataDir, sizeof(sDataDir), "%s", SDL_AndroidGetInternalStoragePath());
#endif
    else
        snprintf(sDataDir, sizeof(sDataDir), "build-JVPPBPort");
    EndWithSlash(sDataDir, sizeof(sDataDir));
    RenameOldFiles();
    dir = getenv("JVPPBPORT_ASSET_DIR");
    snprintf(sAssetDir, sizeof(sAssetDir), "%s", dir && *dir ? dir : sDataDir);
    EndWithSlash(sAssetDir, sizeof(sAssetDir));
}

const char *PortDataPath(char *out, size_t size, const char *name)
{
    if (!sDataDir[0])
        FindDataDir();
    snprintf(out, size, "%s%s", sDataDir, name);
    return out;
}

const char *PortAssetPath(char *out, size_t size, const char *name)
{
    if (!sAssetDir[0])
        FindDataDir();
    snprintf(out, size, "%s%s", sAssetDir, name);
    return out;
}

void PortMakeDir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

// Puts `from` in place of `to` (replacing it if it exists). Returns 1 if it worked.
int PortReplaceFile(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

// The call stack as addresses. Turn them into function names and lines with
//   Linux:   addr2line -f -p -e build-JVPPBPort/jvppbport ADDR...
//   Windows: x86_64-w64-mingw32-addr2line -f -p -e build-JVPPBPort-win/JVPPBPort.debug ADDR...
//            (the debug information kept from that build of the .exe)
//   Android: printed as libmain.so+OFFSET; llvm-addr2line (in the NDK) -f -p -e
//            build-JVPPBPort-android/libmain.debug.so OFFSET...
// (Windows addresses are printed as if the program sat at its preferred
// address, which is what addr2line expects.)
#ifdef _WIN32
// Where the program really sits (the linker's own symbol) and where it would
// like to sit (MinGW's default for 64-bit programs). Taken from the linker
// rather than by reading the program's header in memory: virus scanners'
// guesswork counts header reading as a sign of shellcode (2026-10-23).
extern IMAGE_DOS_HEADER __ImageBase;
#define PREFERRED_BASE 0x140000000ull
#define PROGRAM_SPAN 0x10000000ull  // the program is far smaller; DLLs sit elsewhere

static unsigned long long PreferredAddress(uintptr_t a)
{
    return a - (uintptr_t)&__ImageBase + PREFERRED_BASE;
}
#endif

#ifdef __ANDROID__
// Android has no backtrace() before version 13; this asks the unwinder.
struct BacktraceState
{
    void **frames;
    int count, max;
};

static _Unwind_Reason_Code BacktraceStep(struct _Unwind_Context *context, void *arg)
{
    struct BacktraceState *state = arg;
    uintptr_t pc = _Unwind_GetIP(context);

    if (pc && state->count < state->max)
        state->frames[state->count++] = (void *)pc;
    return state->count < state->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static int Backtrace(void **frames, int max)
{
    struct BacktraceState state = { frames, 0, max };

    _Unwind_Backtrace(BacktraceStep, &state);
    return state.count;
}
#endif

void PortPrintCallStack(void)
{
#ifdef _WIN32
    void *frames[64];
    uintptr_t base = (uintptr_t)&__ImageBase;
    int i, n = CaptureStackBackTrace(0, 64, frames, NULL);

    for (i = 0; i < n; i++)
    {
        uintptr_t a = (uintptr_t)frames[i];
        if (a >= base && a < base + PROGRAM_SPAN)
            fprintf(stderr, "  %#llx\n", PreferredAddress(a));
        else
            fprintf(stderr, "  %#llx (outside the program)\n", (unsigned long long)a);
    }
    fflush(stderr);
#elif defined(__ANDROID__)
    void *frames[64];
    int i, n = Backtrace(frames, 64);

    for (i = 0; i < n; i++)
    {
        Dl_info info;

        if (dladdr(frames[i], &info) && info.dli_fname)
            fprintf(stderr, "  %s+%#lx\n", strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname,
                    (unsigned long)((uintptr_t)frames[i] - (uintptr_t)info.dli_fbase));
        else
            fprintf(stderr, "  %p\n", frames[i]);
    }
    fflush(stderr);
#else
    void *frames[64];
    int n = backtrace(frames, 64);

    backtrace_symbols_fd(frames, n, STDERR_FILENO);
#endif
}

#ifdef _WIN32
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *info)
{
    uintptr_t at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;

    fprintf(stderr, "JVPPBPort: crashed (exception %#lx at %#llx) on frame %u. Call stack:\n",
            (unsigned long)info->ExceptionRecord->ExceptionCode,
            PreferredAddress(at), PortCurrentFrame());
    PortPrintCallStack();
    MessageBoxA(NULL, "JVPPBPort crashed. Details are in jvppbport.log in the data folder.",
                "JVPPBPort", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void CrashHandler(int sig)
{
    fprintf(stderr, "JVPPBPort: crashed (signal %d) on frame %u. Call stack:\n", sig, PortCurrentFrame());
    PortPrintCallStack();
#ifdef __ANDROID__
    // Let Android write its own crash report too (adb logcat shows it).
    signal(sig, SIG_DFL);
    raise(sig);
#endif
    _exit(128 + sig);
}
#endif

void PortSystemInit(void)
{
    char path[1100];

    FindDataDir();
#ifdef _WIN32
    if (getenv("JVPPBPORT_CONSOLE"))
    {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    else
    {
        freopen(PortDataPath(path, sizeof(path), "jvppbport.log"), "w", stderr);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    SetUnhandledExceptionFilter(CrashHandler);
#else
#ifdef __ANDROID__
    freopen(PortDataPath(path, sizeof(path), "jvppbport.log"), "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif
    (void)path;
    signal(SIGSEGV, CrashHandler);
    signal(SIGBUS, CrashHandler);
    signal(SIGFPE, CrashHandler);
#endif
}

// One line in the log (jvppbport.log, or the terminal on Linux), for game code.
void PortLog(const char *fmt, ...)
{
    va_list args;

    fputs("JVPPBPort: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}
