// JVPPBPort: the settings file, jvppbport.ini next to the save file (a test
// save x.sav gets x.ini). Plain text in sections:
//   [Window]     size, place, maximized, fullscreen (platform.c)
//   [PinballKeys], [MenuKeys]  keys set in Options (input.c; [EditKeys] is
//                the older pinball keys of BUTTON CONFIG "EDIT")
//   [Display]    LCD filter, scale (lcd.c, platform.c)
//   [Game]       BallSpeed=Fast: the port's FAST ball speed (field_select.c)
// The whole file is read once and written again on every change; lines the
// program doesn't know are kept.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jvppbport.h"

#define MAX_ENTRIES 128

static struct
{
    char section[32];
    char key[64];
    char value[192];
} sEntries[MAX_ENTRIES];
static int sCount;
static int sLoaded;
static char sPath[1100];

static void Load(void)
{
    char line[300], section[32] = "";
    const char *save = PortSavePath();
    size_t len = strlen(save);
    FILE *f;

    sLoaded = 1;
    if (len > 4 && strcmp(save + len - 4, ".sav") == 0)
        len -= 4;
    snprintf(sPath, sizeof(sPath), "%.*s.ini", (int)len, save);
    f = fopen(sPath, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f) && sCount < MAX_ENTRIES)
    {
        char *eq, *end;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[' && (end = strchr(line, ']')) != NULL)
        {
            *end = '\0';
            snprintf(section, sizeof(section), "%s", line + 1);
            continue;
        }
        if (line[0] == ';' || line[0] == '#' || (eq = strchr(line, '=')) == NULL)
            continue;
        *eq = '\0';
        snprintf(sEntries[sCount].section, sizeof(sEntries[0].section), "%s", section);
        snprintf(sEntries[sCount].key, sizeof(sEntries[0].key), "%s", line);
        snprintf(sEntries[sCount].value, sizeof(sEntries[0].value), "%s", eq + 1);
        sCount++;
    }
    fclose(f);
}

// Forgets what was read (a forked copy of the program with its own
// settings file, see gamedata.c).
void PortSettingsForget(void)
{
    sLoaded = 0;
    sCount = 0;
}

const char *PortSettingGet(const char *section, const char *key)
{
    int i;

    if (!sLoaded)
        Load();
    for (i = 0; i < sCount; i++)
    {
        if (strcmp(sEntries[i].section, section) == 0 && strcmp(sEntries[i].key, key) == 0)
            return sEntries[i].value;
    }
    return NULL;
}

int PortSettingGetInt(const char *section, const char *key, int fallback)
{
    const char *value = PortSettingGet(section, key);
    char *end;
    long n;

    if (!value)
        return fallback;
    n = strtol(value, &end, 10);
    return end != value ? (int)n : fallback;
}

// value NULL removes the setting. Takes effect in the file at PortSettingsSave().
void PortSettingSet(const char *section, const char *key, const char *value)
{
    int i;

    if (!sLoaded)
        Load();
    for (i = 0; i < sCount; i++)
    {
        if (strcmp(sEntries[i].section, section) == 0 && strcmp(sEntries[i].key, key) == 0)
            break;
    }
    if (!value)
    {
        if (i < sCount)
            memmove(&sEntries[i], &sEntries[i + 1], (sCount - i - 1) * sizeof(sEntries[0]));
        sCount -= i < sCount;
        return;
    }
    if (i == sCount)
    {
        if (sCount == MAX_ENTRIES)
            return;
        sCount++;
        snprintf(sEntries[i].section, sizeof(sEntries[0].section), "%s", section);
        snprintf(sEntries[i].key, sizeof(sEntries[0].key), "%s", key);
    }
    snprintf(sEntries[i].value, sizeof(sEntries[0].value), "%s", value);
}

void PortSettingSetInt(const char *section, const char *key, int value)
{
    char text[16];

    snprintf(text, sizeof(text), "%d", value);
    PortSettingSet(section, key, text);
}

void PortSettingsClearSection(const char *section)
{
    int i = 0;

    if (!sLoaded)
        Load();
    while (i < sCount)
    {
        if (strcmp(sEntries[i].section, section) == 0)
            PortSettingSet(section, sEntries[i].key, NULL);
        else
            i++;
    }
}

void PortSettingsSave(void)
{
    char tmp[1110];
    FILE *f;
    int i, j;

    if (!sLoaded)
        Load();
    snprintf(tmp, sizeof(tmp), "%s.tmp", sPath);
    f = fopen(tmp, "w");
    if (!f)
    {
        fprintf(stderr, "JVPPBPort: could not write %s\n", tmp);
        return;
    }
    fprintf(f, "; JVPPBPort settings (the game's own options are in the save file).\n"
               "; [Window]: the window's size and place, kept when the game closes.\n"
               "; [PinballKeys], [MenuKeys]: keys and controller buttons set in Options (PINBALL KEYS, MENU KEYS) by\n"
               "; pressing keys. Names as SDL writes them; a _2 line is a second key.\n"
               "; [Game] BallSpeed=Fast: FAST ball speed (the save file says NORMAL then).\n");
    for (i = 0; i < sCount; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (strcmp(sEntries[j].section, sEntries[i].section) == 0)
                break;
        }
        if (j < i)
            continue;  // section already written
        fprintf(f, "\n[%s]\n", sEntries[i].section);
        for (j = i; j < sCount; j++)
        {
            if (strcmp(sEntries[j].section, sEntries[i].section) == 0)
                fprintf(f, "%s=%s\n", sEntries[j].key, sEntries[j].value);
        }
    }
    fclose(f);
    if (!PortReplaceFile(tmp, sPath))
        fprintf(stderr, "JVPPBPort: could not replace %s\n", sPath);
}
