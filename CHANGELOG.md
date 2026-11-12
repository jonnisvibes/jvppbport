# Changelog

## 0.2.0

### New
* **Android version (experimental public playtest).** The same game on phones
  and tablets (Android 8.0+, 64-bit ARM), asking for your ROM at first start,
  and containing no assets like the Windows version. Only tested on a handful
  of Samsung devices so far: please report problems.
  * On-screen controls, chosen in Options → PINBALL KEYS → TOUCH: **BUTTONS**
    (flipper and pause buttons under the table), **PLAIN** (the same without
    the flipper pictures), **ZONES** (left / right half of the screen) or
    **OFF** (for a controller or keyboard).
  * Swipe on the table to tilt; hold Spoink or the right flipper button to
    launch.
  * Big arrow buttons in the Mart and Evolution Select, a BACK button under
    the Mart, and a BACK button in the corner of every menu.
  * Bigger rows and arrows in Options, which scroll by dragging.
  * Big arrows beside the Ball Speed box in Field Select.
  * Options → SCREEN → SCALE: FILL, SNAP FILL or a fixed size.
  * Controllers and keyboards work, with rumble on the controller or the
    phone's vibration.
  * The game is saved when you leave the app; CONTINUE picks it up if Android
    closed the app meanwhile.
* **Full-size bonus stages:** Options → SCREEN → BONUS STAGE. FULL (the new
  default) shows the whole stage at once; ORIGINAL keeps the GBA's
  scrolling window. The ball plays the same in both.
* **Tate mode** (Windows): Options → SCREEN → TATE MODE, for a monitor turned
  on its side (ON = clockwise, REVERSE = the other way).
* **Safer screen settings:** changes in the SCREEN box now wait for
  **CONFIRM CHANGES**. If the picture changes, **KEEP CHANGES?** counts down
  10 seconds and undoes the change if you don't answer. Leaving Options with
  unconfirmed changes asks to **SAVE** or **DISCARD** them.

### Changed
* **FAST ball speed** is now 35% above NORMAL (was 50%) to reduce occurrences
  of the ball leaving the field, and is still **experimental**.
* The arrows in Options are white with a black outline, like the Options text.

### Fixed
* FAST: when a ball that had left the table was put back on the launcher,
  Ruby's launch ramp could stay closed, so every launch bounced back at the
  overpass by the Mart (and saves kept it that way). The ball is now put back
  like a new ball, and saves stuck this way fix themselves. Each time a ball
  is put back, a line is written to `jvppbport.log`.

### Docs
* README: the Android version and its touch controls, the screen options,
  and how the super-rare Pokémon and e-Reader exclusives work.

## 0.1.0

First public beta (Windows): the whole table on screen at once, no scrolling
camera. Needs your own US ROM; the download contains no game data.
Keyboard, mouse and controllers (all configurable), rumble, LCD / GBA
filters and scaling, real GBA saves, the FAST ball speed, and the
E-READER unlock.
