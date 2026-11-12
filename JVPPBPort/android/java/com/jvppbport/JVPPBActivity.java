// JVPPBPort for Android: SDL's activity plus what the game needs from the
// Java side (called from JVPPBPort/src/android.c).
package com.jvppbport;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Rect;
import android.media.AudioAttributes;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Collections;
import org.libsdl.app.SDLActivity;

public class JVPPBActivity extends SDLActivity {
    private static final int PICK_ROM = 1;
    private static final long MAX_ROM_SIZE = 64L << 20;  // anything bigger is surely not the ROM

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // The flipper buttons are at the bottom corners: keep Android's
        // edge-swipe "back" gesture away from there (it would pause the
        // game when a thumb slides off a button). Android allows this for
        // the lowest 200 dp of each side.
        if (Build.VERSION.SDK_INT >= 29) {
            final View root = getWindow().getDecorView();
            root.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
                @Override
                public void onLayoutChange(View v, int left, int top, int right, int bottom,
                                           int oldLeft, int oldTop, int oldRight, int oldBottom) {
                    int height = bottom - top, limit = (int) (200 * getResources().getDisplayMetrics().density);
                    v.setSystemGestureExclusionRects(
                        Collections.singletonList(new Rect(0, Math.max(0, height - limit), right - left, height)));
                }
            });
        }
    }

    // Opens Android's file picker (setup screen). Called on the game's thread.
    public static void pickRom() {
        final Activity activity = (Activity) SDLActivity.getContext();
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                activity.startActivityForResult(intent, PICK_ROM);
            }
        });
    }

    // The game's rumble (Options -> RUMBLE): the motor on until told off.
    // On is a long buzz that "off" cuts short; marked as a game's, so it
    // follows the phone's media vibration setting.
    public static void rumble(boolean on) {
        Vibrator vibrator = (Vibrator) SDLActivity.getContext().getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator())
            return;
        if (on)
            vibrator.vibrate(VibrationEffect.createOneShot(2000, VibrationEffect.DEFAULT_AMPLITUDE),
                             new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build());
        else
            vibrator.cancel();
    }

    // The same on every connected game controller that has a motor (for
    // pads SDL can't rumble itself). Returns whether any has one.
    public static boolean rumblePads(boolean on) {
        boolean found = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual())
                continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD
                && (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK)
                continue;
            if (Build.VERSION.SDK_INT >= 31) {
                VibratorManager manager = device.getVibratorManager();
                if (manager.getVibratorIds().length == 0)
                    continue;
                found = true;
                if (on)
                    manager.vibrate(CombinedVibration.createParallel(
                        VibrationEffect.createOneShot(2000, VibrationEffect.DEFAULT_AMPLITUDE)));
                else
                    manager.cancel();
            } else {
                Vibrator vibrator = device.getVibrator();
                if (vibrator == null || !vibrator.hasVibrator())
                    continue;
                found = true;
                if (on)
                    vibrator.vibrate(VibrationEffect.createOneShot(2000, VibrationEffect.DEFAULT_AMPLITUDE));
                else
                    vibrator.cancel();
            }
        }
        return found;
    }

    // The picked file is copied into the cache folder and handed to the game
    // as a dropped file; the game deletes the copy after reading it.
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;
        final Uri uri = data.getData();
        final File out = new File(getCacheDir(), "jvppbport-rom.gba");
        new Thread(new Runnable() {
            @Override
            public void run() {
                try (InputStream in = getContentResolver().openInputStream(uri);
                     OutputStream os = new FileOutputStream(out)) {
                    byte[] buffer = new byte[1 << 16];
                    long total = 0;
                    int n;
                    while ((n = in.read(buffer)) > 0 && total < MAX_ROM_SIZE) {
                        os.write(buffer, 0, n);
                        total += n;
                    }
                } catch (Exception e) {
                    out.delete();  // the game then says it can't read the file
                }
                SDLActivity.onNativeDropFile(out.getPath());
            }
        }).start();
    }
}
