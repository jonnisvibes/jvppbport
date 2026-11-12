// JVPPBPort: calls into the Android app's Java side
// (JVPPBPort/android/java/com/jvppbport/JVPPBActivity.java). Empty on other
// systems.
#ifndef __ANDROID__
#include "jvppbport.h"

void PortAndroidVibrate(int on)
{
    (void)on;
}

int PortAndroidPadVibrate(int on)
{
    (void)on;
    return 0;
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include <SDL.h>
#include "jvppbport.h"

// The name the Java side gives its copy of the picked file (in the app's cache folder).
#define PICKED_NAME "/jvppbport-rom.gba"

// Calls a static method of the app's activity that takes nothing, or one
// boolean (arg >= 0), and returns nothing.
static void CallActivity(const char *method, int arg)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    jclass cls;
    jmethodID id;

    if (!env || !activity)
        return;
    // The activity's own class: FindClass can't see the app's classes from this thread.
    cls = (*env)->GetObjectClass(env, activity);
    id = (*env)->GetStaticMethodID(env, cls, method, arg < 0 ? "()V" : "(Z)V");
    if (id && arg < 0)
        (*env)->CallStaticVoidMethod(env, cls, id);
    else if (id)
        (*env)->CallStaticVoidMethod(env, cls, id, (jboolean)(arg != 0));
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        fprintf(stderr, "JVPPBPort: Java method %s failed\n", method);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}

// Opens Android's file picker. The Java side copies the chosen file into the
// app's cache folder and hands it over as if dropped on the window
// (SDL_DROPFILE), so the setup screen takes it like a dropped file.
void PortAndroidPickRom(void)
{
    CallActivity("pickRom", -1);
}

// Deletes the copy made by the file picker once the setup screen has read it.
void PortAndroidPickedFileDone(const char *path)
{
    size_t len = strlen(path);

    if (len > strlen(PICKED_NAME) && strcmp(path + len - strlen(PICKED_NAME), PICKED_NAME) == 0)
        remove(path);
}

// Test mode in the emulator: testenv.txt in the app's data folder holds
// JVPPBPORT_ settings, one NAME=value per line, read at the start (Android
// apps get no environment from adb). Delete the file after testing.
void PortAndroidTestEnv(void)
{
    char path[1024], line[1024];
    const char *dir = SDL_AndroidGetExternalStoragePath();
    FILE *f;

    if (!dir)
        return;
    snprintf(path, sizeof(path), "%s/testenv.txt", dir);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');

        line[strcspn(line, "\r\n")] = '\0';
        if (eq && strncmp(line, "JVPPBPORT_", 10) == 0)
        {
            *eq = '\0';
            setenv(line, eq + 1, 1);
            fprintf(stderr, "JVPPBPort: test setting %s=%s\n", line, eq + 1);
        }
    }
    fclose(f);
}

// The phone's vibration motor (the game's rumble, see PortRumble in
// pad.c): on until told off.
void PortAndroidVibrate(int on)
{
    CallActivity("rumble", on != 0);
}

// The connected controllers' vibration motors, as Android reaches them (for
// pads SDL can't rumble itself). Returns 1 if a controller has one.
int PortAndroidPadVibrate(int on)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    jclass cls;
    jmethodID id;
    int done = 0;

    if (!env || !activity)
        return 0;
    cls = (*env)->GetObjectClass(env, activity);
    id = (*env)->GetStaticMethodID(env, cls, "rumblePads", "(Z)Z");
    if (id)
        done = (*env)->CallStaticBooleanMethod(env, cls, id, (jboolean)(on != 0));
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        fprintf(stderr, "JVPPBPort: Java method rumblePads failed\n");
        done = 0;
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return done;
}
#endif
