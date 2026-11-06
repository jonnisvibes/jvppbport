// JVPPBPort: the game's data (graphics, sound, tables), taken from the
// player's own ROM.
//
// The program holds no byte of the game's data. data2c.py leaves zeros where
// those bytes go and lists every such place (gPortRomFixups: where, which ROM
// offset, how many bytes). The first time the game starts, the setup screen
// (setup.c) asks for the ROM; PortGameDataFromRom checks it is the right one
// (its SHA-1) and keeps the part the data comes from (ROM offsets
// gPortRomDataStart..End, about 6.6 MB) in gamedata.bin in the asset folder.
// Every start, PortGameDataLoad fills the zeros in from that file before the
// game runs. gamedata.bin stays good for later versions of the program
// unless one needs a part of the ROM outside the kept range (then the setup
// screen asks for the ROM again).
//
// The pictures of the two whole tables (gPortTablePictures, for Field Select
// and the bonus stages) are drawn by the game itself: a hidden copy of the
// program plays each table for a moment and saves it (JVPPBPORT_TABLEDUMP,
// ppu.c). On Android the copy is made with fork() and has no window. They are kept in tables.bin and retaken when the program changes
// (a different gPortBuildId), which takes a few seconds and needs no ROM.
//
// Asset folder: the data folder (system.c), or JVPPBPORT_ASSET_DIR.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "constants/fields.h"
#include "jvppbport.h"

// Pokémon Pinball: Ruby & Sapphire (USA), the ROM pret's decompilation matches.
#define ROM_SIZE 0x800000
static const char sRomSha1[] = "9fec81ce2c5df589e0371a0bf2f92a5fe8db730b";

#define GAMEDATA_FILE "gamedata.bin"
#define TABLES_FILE "tables.bin"
#define GAMEDATA_MAGIC "JCPPBDAT"
#define TABLES_MAGIC "JCPPBTBL"
#define FILE_VERSION 1
#define BUILD_ID_SIZE 64
#define TABLE_PIXELS (PORT_TABLE_WIDTH * PORT_TABLE_HEIGHT)

// Made by data2c.py (rom_fixups.s) and the Makefile (build_id.c).
struct PortRomFixup
{
    void *dest;
    uint32_t romOffset, size;
};
extern const struct PortRomFixup gPortRomFixups[];
extern const uint32_t gPortRomFixupCount, gPortRomDataStart, gPortRomDataEnd;
extern const char gPortBuildId[];

const uint32_t *gPortTablePictures;

// ---- SHA-1 ----

struct Sha1
{
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    int used;
};

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void Sha1Block(struct Sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a, b, c, d, e, f, k, t;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (; i < 80; i++)
        w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (i = 0; i < 80; i++)
    {
        if (i < 20)
            f = (b & c) | (~b & d), k = 0x5A827999;
        else if (i < 40)
            f = b ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60)
            f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        else
            f = b ^ c ^ d, k = 0xCA62C1D6;
        t = ROL(a, 5) + f + e + k + w[i];
        e = d, d = c, c = ROL(b, 30), b = a, a = t;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

// The SHA-1 of data, as 40 lowercase hex digits.
static void Sha1Hex(const void *data, size_t size, char out[41])
{
    struct Sha1 s = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 } };
    const uint8_t *p = data;
    uint8_t tail[128] = { 0 };
    size_t rest = size % 64, tailSize = rest < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    int i;

    for (; size >= 64; size -= 64, p += 64)
        Sha1Block(&s, p);
    memcpy(tail, p, rest);
    tail[rest] = 0x80;
    for (i = 0; i < 8; i++)
        tail[tailSize - 1 - i] = (uint8_t)(bits >> (i * 8));
    Sha1Block(&s, tail);
    if (tailSize == 128)
        Sha1Block(&s, tail + 64);
    for (i = 0; i < 5; i++)
        sprintf(out + i * 8, "%08x", s.h[i]);
}

// ---- small file helpers ----

static void *ReadWholeFile(const char *path, size_t maxSize, size_t *size)
{
    FILE *f = fopen(path, "rb");
    void *data = NULL;
    long len;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && (size_t)len <= maxSize
        && fseek(f, 0, SEEK_SET) == 0 && (data = malloc(len ? len : 1)) != NULL
        && fread(data, 1, len, f) != (size_t)len)
    {
        free(data);
        data = NULL;
    }
    if (data)
        *size = len;
    fclose(f);
    return data;
}

// Writes the parts to `name` in the asset folder, through a .tmp file so a
// half-written file never replaces a good one.
static int WriteAssetFile(const char *name, const void *a, size_t aSize, const void *b, size_t bSize)
{
    char path[1100], tmp[1110];
    FILE *f;
    int ok;

    PortAssetPath(path, sizeof(path), "");
    PortMakeDir(path);
    PortAssetPath(path, sizeof(path), name);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;
    ok = fwrite(a, 1, aSize, f) == aSize && fwrite(b, 1, bSize, f) == bSize;
    ok = fclose(f) == 0 && ok;
    if (!ok || !PortReplaceFile(tmp, path))
    {
        remove(tmp);
        return 0;
    }
    return 1;
}

// ---- gamedata.bin ----

struct GameDataHeader
{
    char magic[8];
    uint32_t version;
    uint32_t start, size;  // the ROM offsets kept
    char sha1[40];         // of the bytes kept (to notice a damaged file)
};

static const char *sGameDataProblem;

int PortGameDataLoad(void)
{
    char path[1100], sha1[41];
    struct GameDataHeader header;
    size_t fileSize;
    uint8_t *file, *bytes;
    uint32_t i;

    sGameDataProblem = NULL;
    file = ReadWholeFile(PortAssetPath(path, sizeof(path), GAMEDATA_FILE), ROM_SIZE + 4096, &fileSize);
    if (!file)
        return 0;  // not set up yet
    memcpy(&header, file, fileSize >= sizeof(header) ? sizeof(header) : 0);
    bytes = file + sizeof(header);
    if (fileSize < sizeof(header) || memcmp(header.magic, GAMEDATA_MAGIC, 8) != 0
        || header.version != FILE_VERSION || fileSize != sizeof(header) + header.size
        || (Sha1Hex(bytes, header.size, sha1), memcmp(sha1, header.sha1, 40) != 0))
        sGameDataProblem = "THE SAVED GAME DATA IS DAMAGED.";
    else if (header.start > gPortRomDataStart || header.start + header.size < gPortRomDataEnd)
        sGameDataProblem = "THIS VERSION NEEDS THE ROM ONCE MORE.";
    if (sGameDataProblem)
    {
        fprintf(stderr, "JVPPBPort: %s: %s\n", path, sGameDataProblem);
        free(file);
        return 0;
    }
    for (i = 0; i < gPortRomFixupCount; i++)
        memcpy(gPortRomFixups[i].dest, bytes + (gPortRomFixups[i].romOffset - header.start), gPortRomFixups[i].size);
    free(file);
    return 1;
}

// Why the last PortGameDataLoad found gamedata.bin unusable, or NULL if it
// was just missing (or loaded).
const char *PortGameDataProblem(void)
{
    return sGameDataProblem;
}

// Checks that `path` is the right ROM and keeps its data part. Returns NULL
// if that worked, else what went wrong (in the port's font's letters).
const char *PortGameDataFromRom(const char *path)
{
    struct GameDataHeader header = { GAMEDATA_MAGIC, FILE_VERSION };
    char sha1[41];
    size_t size;
    uint8_t *rom = ReadWholeFile(path, 64 << 20, &size);

    if (!rom)
        return "CAN'T READ THAT FILE.";
    if (size >= 2 && rom[0] == 'P' && rom[1] == 'K')
    {
        free(rom);
        return "THAT'S A ZIP FILE. UNZIP IT FIRST.";
    }
    if (size == ROM_SIZE)
        Sha1Hex(rom, size, sha1);
    if (size != ROM_SIZE || strcmp(sha1, sRomSha1) != 0)
    {
        free(rom);
        fprintf(stderr, "JVPPBPort: %s is not the expected ROM (size %zu)\n", path, size);
        return "THAT'S NOT THE RIGHT ROM.";
    }
    header.start = gPortRomDataStart;
    header.size = gPortRomDataEnd - gPortRomDataStart;
    Sha1Hex(rom + header.start, header.size, sha1);
    memcpy(header.sha1, sha1, 40);
    if (!WriteAssetFile(GAMEDATA_FILE, &header, sizeof(header), rom + header.start, header.size))
    {
        free(rom);
        return "CAN'T SAVE THE GAME DATA.";
    }
    free(rom);
    fprintf(stderr, "JVPPBPort: game data taken from %s\n", path);
    return NULL;
}

// ---- tables.bin ----

struct TablesHeader
{
    char magic[8];
    uint32_t version;
    char buildId[BUILD_ID_SIZE];
};

int PortTablesLoad(void)
{
    static uint32_t *pictures;
    char path[1100];
    struct TablesHeader header;
    size_t size;
    uint8_t *file = ReadWholeFile(PortAssetPath(path, sizeof(path), TABLES_FILE),
                                  sizeof(header) + MAIN_FIELD_COUNT * TABLE_PIXELS * 4, &size);

    if (!file)
        return 0;
    memcpy(&header, file, size >= sizeof(header) ? sizeof(header) : 0);
    if (size != sizeof(header) + MAIN_FIELD_COUNT * TABLE_PIXELS * 4 || memcmp(header.magic, TABLES_MAGIC, 8) != 0
        || header.version != FILE_VERSION || strncmp(header.buildId, gPortBuildId, BUILD_ID_SIZE) != 0)
    {
        free(file);
        return 0;  // made by another version of the program: retake
    }
    free(pictures);
    pictures = malloc(MAIN_FIELD_COUNT * TABLE_PIXELS * 4);
    if (!pictures)
    {
        free(file);
        return 0;
    }
    memcpy(pictures, file + sizeof(header), MAIN_FIELD_COUNT * TABLE_PIXELS * 4);
    free(file);
    gPortTablePictures = pictures;
    return 1;
}

// One hidden copy of the program per table, run with the test-mode settings
// the Makefile used to take the pictures with, each in its own folder (for
// its save file and log). Its other JVPPBPORT_ settings are left out.
#define TABLE_TIMEOUT_MS 120000

static struct
{
    char dir[1100];
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
    int state;  // 0 running, 1 done, -1 failed
} sJobs[MAIN_FIELD_COUNT];
static uint32_t sTablesStartTime;

static const char *const sJobFiles[] = { "table.bin", "table.bin.tmp", "t.sav", "t.sav.tmp", "t.ini", "t.ini.tmp", "jvppbport.log" };

static void CleanJob(int i)
{
    char path[2400];
    size_t k;

    for (k = 0; k < sizeof(sJobFiles) / sizeof(sJobFiles[0]); k++)
    {
        snprintf(path, sizeof(path), "%s/%s", sJobs[i].dir, sJobFiles[k]);
        remove(path);
    }
#ifdef _WIN32
    RemoveDirectoryA(sJobs[i].dir);
#else
    rmdir(sJobs[i].dir);
#endif
}

// The environment for a table job: ours minus JVPPBPORT_ settings, plus the job's.
static char **JobEnvironment(int i, char **env)
{
    static char settings[9][2400];
    char asset[1100];
    char **out;
    int n = 0, k;

    for (k = 0; env[k]; k++)
        ;
    out = malloc((k + 10) * sizeof(*out));
    for (k = 0; env[k]; k++)
    {
        if (strncmp(env[k], "JVPPBPORT_", 10) != 0)
            out[n++] = env[k];
    }
    PortAssetPath(asset, sizeof(asset), "");
    snprintf(settings[0], sizeof(settings[0]), "JVPPBPORT_DATA_DIR=%s", sJobs[i].dir);
    snprintf(settings[1], sizeof(settings[1]), "JVPPBPORT_ASSET_DIR=%s", asset);
    snprintf(settings[2], sizeof(settings[2]), "JVPPBPORT_SAVE=%s/t.sav", sJobs[i].dir);
    snprintf(settings[3], sizeof(settings[3]), "JVPPBPORT_TABLEDUMP=%s/table.bin.tmp", sJobs[i].dir);
    snprintf(settings[4], sizeof(settings[4]), "JVPPBPORT_TEST_FIELD=%d", i);
    snprintf(settings[5], sizeof(settings[5]), "JVPPBPORT_PRESS=1850:START,1950:A,2150:A,2850:A");
    snprintf(settings[6], sizeof(settings[6]), "JVPPBPORT_EXIT=20000");
    snprintf(settings[7], sizeof(settings[7]), "JVPPBPORT_FAST=1");
    snprintf(settings[8], sizeof(settings[8]), "JVPPBPORT_MUTE=1");
    for (k = 0; k < 9; k++)
        out[n++] = settings[k];
    out[n] = NULL;
    return out;
}

#ifdef _WIN32
static int StartJob(int i)
{
    char exe[MAX_PATH], cmd[MAX_PATH + 3], *block, *p, **env;
    char *strings = GetEnvironmentStringsA();
    size_t size = 0;
    int count = 0, k;
    char **list;
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL ok;

    // Windows keeps the environment as one block of "NAME=value\0" strings.
    for (p = strings; *p; p += strlen(p) + 1)
        count++;
    list = malloc((count + 1) * sizeof(*list));
    count = 0;
    for (p = strings; *p; p += strlen(p) + 1)
        list[count++] = p;
    list[count] = NULL;
    env = JobEnvironment(i, list);
    for (k = 0; env[k]; k++)
        size += strlen(env[k]) + 1;
    block = p = malloc(size + 1);
    for (k = 0; env[k]; k++)
    {
        strcpy(p, env[k]);
        p += strlen(p) + 1;
    }
    *p = '\0';
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    snprintf(cmd, sizeof(cmd), "\"%s\"", exe);
    ok = CreateProcessA(exe, cmd, NULL, NULL, 0, 0, block, NULL, &si, &pi);
    free(block);
    free(env);
    free(list);
    FreeEnvironmentStringsA(strings);
    if (!ok)
        return 0;
    CloseHandle(pi.hThread);
    sJobs[i].process = pi.hProcess;
    return 1;
}

// 0 still running, 1 exited normally, -1 failed. stop: end it now.
static int PollJob(int i, int stop)
{
    DWORD code;

    if (stop)
        TerminateProcess(sJobs[i].process, 1);
    if (WaitForSingleObject(sJobs[i].process, stop ? INFINITE : 0) != WAIT_OBJECT_0)
        return 0;
    GetExitCodeProcess(sJobs[i].process, &code);
    CloseHandle(sJobs[i].process);
    return code == 0 && !stop ? 1 : -1;
}
#else
extern char **environ;

#ifdef __ANDROID__
// An app can't start its own program file on Android, so the copy is made
// with fork() alone: it starts as an exact copy of this program (still at
// the setup screen, the game not yet started) and runs the game again from
// the start without a window (PortRunForkedCopy).
static int StartJob(int i)
{
    char **env = JobEnvironment(i, environ);
    pid_t pid;

    fflush(NULL);
    pid = fork();
    if (pid == 0)
    {
        int k;

        for (k = 0; environ[k]; )
        {
            char name[64];
            size_t len = strcspn(environ[k], "=");

            if (strncmp(environ[k], "JVPPBPORT_", 10) == 0 && len < sizeof(name))
            {
                snprintf(name, sizeof(name), "%.*s", (int)len, environ[k]);
                unsetenv(name);
            }
            else
                k++;
        }
        for (k = 0; env[k]; k++)
        {
            if (strncmp(env[k], "JVPPBPORT_", 10) == 0)
                putenv(env[k]);
        }
        PortRunForkedCopy();
    }
#else
static int StartJob(int i)
{
    char exe[1100];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char **env;
    pid_t pid;

    if (len <= 0)
        return 0;
    exe[len] = '\0';
    env = JobEnvironment(i, environ);
    pid = fork();
    if (pid == 0)
    {
        char *argv[] = { exe, NULL };
        execve(exe, argv, env);
        _exit(127);
    }
#endif
    free(env);
    sJobs[i].pid = pid;
    return pid > 0;
}

static int PollJob(int i, int stop)
{
    int status;

    if (stop)
        kill(sJobs[i].pid, SIGKILL);
    if (waitpid(sJobs[i].pid, &status, stop ? 0 : WNOHANG) != sJobs[i].pid)
        return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && !stop ? 1 : -1;
}
#endif

// Starts making the table pictures. Returns 0 if that couldn't start.
int PortTablesStart(void)
{
    char dir[1100];
    int i;

    for (i = 0; i < MAIN_FIELD_COUNT; i++)
    {
        snprintf(dir, sizeof(dir), "tablework%d", i);
        PortAssetPath(sJobs[i].dir, sizeof(sJobs[i].dir), dir);
        PortMakeDir(sJobs[i].dir);
        sJobs[i].state = StartJob(i) ? 0 : -1;
        if (sJobs[i].state < 0)
            fprintf(stderr, "JVPPBPort: couldn't start the program again to draw table %d\n", i);
    }
    sTablesStartTime = SDL_GetTicks();
    return sJobs[0].state == 0 || sJobs[1].state == 0;
}

// 0 while the table pictures are being made, 1 when they're made and
// loaded, -1 if that failed (the game then runs without them).
int PortTablesPoll(void)
{
    static uint32_t pictures[MAIN_FIELD_COUNT * TABLE_PIXELS];
    struct TablesHeader header = { TABLES_MAGIC, FILE_VERSION };
    int timedOut = SDL_GetTicks() - sTablesStartTime > TABLE_TIMEOUT_MS;
    int i, ok = 1;

    for (i = 0; i < MAIN_FIELD_COUNT; i++)
    {
        if (sJobs[i].state == 0)
            sJobs[i].state = PollJob(i, timedOut);
        if (sJobs[i].state == 0)
            return 0;
    }
    for (i = 0; i < MAIN_FIELD_COUNT; i++)
    {
        char path[2400];
        FILE *f;

        snprintf(path, sizeof(path), "%s/table.bin.tmp", sJobs[i].dir);
        f = fopen(path, "rb");
        if (sJobs[i].state != 1 || !f || fread(&pictures[i * TABLE_PIXELS], 4, TABLE_PIXELS, f) != TABLE_PIXELS)
        {
            // Its folder is left as it is (with its log, on Windows).
            fprintf(stderr, "JVPPBPort: drawing table %d failed (see %s)\n", i, sJobs[i].dir);
            ok = 0;
            if (f)
                fclose(f);
            continue;
        }
        fclose(f);
        CleanJob(i);
    }
    strncpy(header.buildId, gPortBuildId, BUILD_ID_SIZE);
    if (!ok || !WriteAssetFile(TABLES_FILE, &header, sizeof(header), pictures, sizeof(pictures)) || !PortTablesLoad())
        return -1;
    return 1;
}
