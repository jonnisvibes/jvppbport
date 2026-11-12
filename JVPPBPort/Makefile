# JVPPBPort: builds the PC version of the game with the host's gcc and SDL2.
# Run from the repo root with `make jvppbport` (after a normal `make`: the
# data tables are laid out from the GBA build's data objects).
#
# The program contains none of the game's data: the data tables are zeros
# that get filled in at startup from the player's own ROM (see
# JVPPBPort/src/gamedata.c). It asks for the ROM the first time it starts.
#
# `make jvppbport-win` (WINDOWS=1 here) builds the Windows version instead:
# build-JVPPBPort-win/JVPPBPort.exe, one file with SDL2 and the C runtime
# built in. It uses MinGW-w64 (apt package mingw-w64) and SDL2's official
# MinGW development package (downloaded into build-JVPPBPort-win/sdl/ the
# first time), and reuses the data the Linux build made in build-JVPPBPort/gen/
# (the top Makefile builds that first).
#
# `make jvppbport-android` (ANDROID=1 here) builds the Android version:
# build-JVPPBPort-android/JVPPBPort.apk, for 64-bit ARM phones and tablets
# (Android 8 and later). No Gradle: the NDK's clang builds SDL2 (with SDL's
# own Android.mk) and the game as libSDL2.so and libmain.so, javac + d8 turn
# SDL's Java side and ours (JVPPBPort/android/java) into classes.dex, and
# aapt2, zipalign and apksigner make the .apk. Needs the Android SDK in
# ~/Android/sdk (ANDROID_HOME) with the NDK, build-tools, platform and
# platform-tools versions below, and Java 17. SDL2's source is downloaded into
# build-JVPPBPort-android/sdl/ the first time. The .apk is signed with
# ~/Android/jcppbport.keystore, whose password is in jcppbport.keystore.pass
# next to it (both made the first time, never in the repo; keep them: an .apk
# signed with another key can't update the installed game, and uninstalling
# it deletes the save). Like Windows, it reuses the Linux build's data.
# RELEASE=1 makes the public .apk: not debuggable, and its version code is
# exactly the port version's (0.2.0 = 20000, as in VERSION_CODE below). Test
# builds are debuggable and count up from there with the APK build number, so
# a phone can always install the next test build over the last one.

PREPROC   := tools/preproc/preproc
# PREGEN=1 (the public repo, made by JVPPBPort/tools/export_public.sh): the
# data table layouts are the pre-made ones in JVPPBPort/gen/ (they hold no
# game data, only labels, zeros and addresses), so no GBA build is needed.
ifeq ($(PREGEN),1)
LINUX_GEN := JVPPBPort/gen
else
LINUX_GEN := build-JVPPBPort/gen
endif

ifeq ($(WINDOWS),1)
BUILD    := build-JVPPBPort-win
TARGET   := $(BUILD)/JVPPBPort.exe
CC       := x86_64-w64-mingw32-gcc
WINDRES  := x86_64-w64-mingw32-windres
SDL_VERSION := 2.32.10
SDL_SHA256  := 83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
SDL_TAR  := $(BUILD)/sdl/SDL2-devel-$(SDL_VERSION)-mingw.tar.gz
SDL_DIR  := $(BUILD)/sdl/SDL2-$(SDL_VERSION)/x86_64-w64-mingw32
SDL_CFLAGS := -I$(SDL_DIR)/include/SDL2 -Dmain=SDL_main -D__USE_MINGW_ANSI_STDIO=1
# SDL's own list of what a static SDL2 needs (its sdl2-config --static-libs),
# plus -static for the C runtime, so the .exe needs no DLLs. -mwindows: no
# console window (messages go to a log file, see JVPPBPort/src/system.c).
SDL_LIBS   := -L$(SDL_DIR)/lib -lmingw32 -lSDL2main $(SDL_DIR)/lib/libSDL2.a -mwindows \
              -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -lm -ldinput8 -ldxguid -ldxerr8 \
              -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid -lcomdlg32 \
              -static
GEN_SRC_DIR := $(LINUX_GEN)
else ifeq ($(ANDROID),1)
BUILD    := build-JVPPBPort-android
TARGET   := $(BUILD)/JVPPBPort.apk
ANDROID_HOME ?= $(HOME)/Android/sdk
NDK      := $(ANDROID_HOME)/ndk/28.2.13676358
BUILD_TOOLS := $(ANDROID_HOME)/build-tools/36.0.0
ANDROID_JAR := $(ANDROID_HOME)/platforms/android-35/android.jar
MIN_SDK  := 26
TARGET_SDK := 35
NDK_BIN  := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
CC       := $(NDK_BIN)/aarch64-linux-android$(MIN_SDK)-clang
# Signing keys: JVPPBPort/private/ when it exists (the private repo keeps
# them there, never exported), otherwise ~/Android/ (made the first time).
# Release builds use their own release key; test builds the older test key.
KEYDIR   := $(if $(wildcard JVPPBPort/private/*.keystore),JVPPBPort/private,$(HOME)/Android)
ifeq ($(RELEASE),1)
KEYSTORE ?= $(KEYDIR)/jvppbport-release.keystore
else
KEYSTORE ?= $(KEYDIR)/jcppbport.keystore
endif
KEYSTORE_PASS := $(KEYSTORE).pass
SDL_VERSION := 2.32.10
SDL_SHA256  := 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL_TAR  := $(BUILD)/sdl/SDL2-$(SDL_VERSION).tar.gz
SDL_SRC  := $(BUILD)/sdl/SDL2-$(SDL_VERSION)
APK_LIBS := $(BUILD)/apk/lib/arm64-v8a
# -fsigned-char and -ffp-contract=off: compute exactly like the PC builds
# (ARM's char is unsigned, and clang would otherwise fuse a*b+c in floats).
SDL_CFLAGS := -I$(SDL_SRC)/include -fPIC -fsigned-char -ffp-contract=off -Wno-unknown-warning-option
SDL_LIBS   := -shared -L$(APK_LIBS) -lSDL2 -llog -landroid -Wl,-Bsymbolic -Wl,-z,max-page-size=16384
GEN_SRC_DIR := $(LINUX_GEN)
else
BUILD    := build-JVPPBPort
TARGET   := $(BUILD)/jvppbport
CC       := gcc
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)
GEN_SRC_DIR := $(LINUX_GEN)
endif

CPPFLAGS := -std=gnu11 -DJVPPBPORT -iquote include -I JVPPBPort/include $(SDL_CFLAGS)
CFLAGS   := -O2 -g -ffunction-sections -fno-strict-aliasing -fwrapv -fcommon -fno-aggressive-loop-optimizations \
            -Wno-incompatible-pointer-types -Wno-int-conversion \
            -Wno-implicit-function-declaration -Wno-return-mismatch \
            -Wno-builtin-declaration-mismatch -Wno-int-to-pointer-cast \
            -Wno-pointer-to-int-cast
ifeq ($(ANDROID),1)
CFLAGS   := $(filter-out -fno-aggressive-loop-optimizations,$(CFLAGS))
endif
LDLIBS   := $(SDL_LIBS) -lm
# Each function in its own section (-ffunction-sections), so the linker drops
# the ones nothing calls. (Not -fdata-sections: MinGW would store the zeroed
# RAM variables in the .exe.)
LDFLAGS  := -Wl,--gc-sections
# Game files (and generated files that use game structs) get the GBA's
# struct layout rule; see JVPPBPort/include/jvppbport_layout.h.
GAME_CPPFLAGS := -include jvppbport_layout.h
CFLAGS   += -Wno-attributes
# Debug info names files relative to the repo, not the folder it was built in
# (that path holds the builder's user name, and the .debug file is shared).
CFLAGS   += -ffile-prefix-map=$(CURDIR)=.
ifeq ($(ANDROID),1)
# The NDK lives in the home folder, so its header paths would name the user too.
CFLAGS   += -ffile-prefix-map=$(abspath $(NDK))=ndk
endif

# Game sources from pret, minus the ones the port replaces.
GAME_EXCLUDE := src/agb_sram.c
GAME_SRCS := $(filter-out $(GAME_EXCLUDE),$(wildcard src/*.c))
PORT_SRCS := $(wildcard JVPPBPort/src/*.c)
DATA_OBJS := $(addprefix build/pokepinballrs/data/,rom_1.o rom_2.o sound_data.o sound_data_2.o)
GEN_NAMES := rom_1.s rom_2.s sound_data.s sound_data_2.s rom_fixups.s ram_syms.s ram_aliases.c wrap_rows.c
GEN_SRCS  := $(addprefix $(GEN_SRC_DIR)/,$(GEN_NAMES))

GAME_OBJS := $(patsubst src/%.c,$(BUILD)/game/%.o,$(GAME_SRCS))
PORT_OBJS := $(patsubst JVPPBPort/src/%.c,$(BUILD)/port/%.o,$(PORT_SRCS))
GEN_OBJS  := $(addprefix $(BUILD)/gen/,$(patsubst %.c,%.o,$(GEN_NAMES:.s=.o)))
LINK_OBJS := $(GAME_OBJS) $(PORT_OBJS) $(GEN_OBJS)

.PHONY: all objs clean
all: $(TARGET)
objs: $(GAME_OBJS) $(PORT_OBJS)

ifeq ($(WINDOWS),1)

# The debug information goes into JVPPBPort.debug next to the .exe (keep it
# with each release: crash call stacks in the log are turned into function
# names with it, see JVPPBPort/src/system.c); the .exe itself is stripped.
$(TARGET): $(LINK_OBJS) $(BUILD)/gen/build_id.o $(BUILD)/gen/resources.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	x86_64-w64-mingw32-objcopy --only-keep-debug $@ $(BUILD)/JVPPBPort.debug
	x86_64-w64-mingw32-strip --strip-debug $@
	cd $(BUILD) && x86_64-w64-mingw32-objcopy --add-gnu-debuglink=JVPPBPort.debug JVPPBPort.exe

# The .exe's icon (the red and blue ball logo drawn by makeicon.py) and version
# information (the name Windows shows for it).
$(BUILD)/gen/resources.o: JVPPBPort/windows/jvppbport.rc JVPPBPort/include/version.h $(BUILD)/gen/jvppbport.ico
	$(WINDRES) -I $(BUILD)/gen -I JVPPBPort/include $< -o $@

$(BUILD)/gen/jvppbport.ico: JVPPBPort/tools/makeicon.py
	@mkdir -p $(dir $@)
	python3 JVPPBPort/tools/makeicon.py $@

# SDL2's MinGW development package, as released by SDL (checked by its SHA-256).
$(SDL_TAR):
	@mkdir -p $(dir $@)
	curl -sSL -o $@.tmp https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-devel-$(SDL_VERSION)-mingw.tar.gz
	echo "$(SDL_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

$(SDL_DIR)/lib/libSDL2.a: $(SDL_TAR)
	tar xzf $< -C $(BUILD)/sdl
	touch $@

$(GAME_OBJS) $(PORT_OBJS): | $(SDL_DIR)/lib/libSDL2.a

# The data tables and RAM symbols are the Linux build's.
# The RAM variables, minus the ones the C code defines (see winsyms.py).
$(BUILD)/gen/ram_syms.o: $(LINUX_GEN)/ram_syms.s $(GAME_OBJS) $(PORT_OBJS) $(BUILD)/gen/ram_aliases.o JVPPBPort/tools/winsyms.py
	x86_64-w64-mingw32-nm -g --defined-only $(GAME_OBJS) $(PORT_OBJS) $(BUILD)/gen/ram_aliases.o \
	    | awk 'NF == 3 && $$2 != "C" { print $$3 }' > $(BUILD)/gen/c_defined.txt
	python3 JVPPBPort/tools/winsyms.py $< $(BUILD)/gen/c_defined.txt $(BUILD)/gen/ram_syms_win.s
	$(CC) -c -x assembler $(BUILD)/gen/ram_syms_win.s -o $@

# The generated assembly is written for Linux; Windows objects have no
# .note.GNU-stack section.
$(BUILD)/gen/%.o: $(LINUX_GEN)/%.s
	@mkdir -p $(dir $@)
	sed '/\.note\.GNU-stack/d' $< | $(CC) -c -x assembler - -o $@

$(BUILD)/gen/%.o: $(LINUX_GEN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GAME_CPPFLAGS) -c $< -o $@

else ifeq ($(ANDROID),1)

ANDROID_TOOLS := $(NDK)/ndk-build $(BUILD_TOOLS)/aapt2 $(ANDROID_JAR)
$(ANDROID_TOOLS):
	@echo "Missing $@: install the Android SDK parts listed at the top of JVPPBPort/Makefile"; exit 1

$(TARGET): $(BUILD)/unsigned.apk $(KEYSTORE)
	rm -f $@.aligned
	$(BUILD_TOOLS)/zipalign -P 16 -f 4 $< $@.aligned
	$(BUILD_TOOLS)/apksigner sign --ks $(KEYSTORE) --ks-pass file:$(KEYSTORE_PASS) --out $@ $@.aligned
	rm -f $@.aligned $@.idsig

# A new key with a random password.
$(KEYSTORE):
	@test ! -e $(KEYSTORE_PASS) || { echo "$(KEYSTORE_PASS) exists but $@ doesn't"; exit 1; }
	(umask 077 && head -c 18 /dev/urandom | base64 > $(KEYSTORE_PASS))
	@mkdir -p $(dir $@)
	keytool -genkeypair -keystore $@ -storepass:file $(KEYSTORE_PASS) -alias jvppbport -keyalg RSA -keysize 4096 \
	    -validity 36500 -dname "CN=JVPPBPort"

# The game and SDL2 as libraries (SDLActivity loads libSDL2.so and libmain.so).
# The debug information stays in $(BUILD)/libmain.debug.so (to turn crash
# offsets in the log into function names); the .apk gets a stripped copy.
$(APK_LIBS)/libmain.so: $(LINK_OBJS) $(BUILD)/gen/build_id.o $(APK_LIBS)/libSDL2.so
	$(CC) $(LDFLAGS) -o $(BUILD)/libmain.debug.so $(LINK_OBJS) $(BUILD)/gen/build_id.o $(LDLIBS)
	$(NDK_BIN)/llvm-objcopy --strip-debug $(BUILD)/libmain.debug.so $@

$(SDL_TAR):
	@mkdir -p $(dir $@)
	curl -sSL -o $@.tmp https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-$(SDL_VERSION).tar.gz
	echo "$(SDL_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

$(SDL_SRC)/Android.mk: $(SDL_TAR)
	tar xzf $< -C $(BUILD)/sdl
	touch $@

$(APK_LIBS)/libSDL2.so: $(SDL_SRC)/Android.mk | $(NDK)/ndk-build
	$(NDK)/ndk-build NDK_PROJECT_PATH=null APP_BUILD_SCRIPT=$(abspath $<) NDK_OUT=$(BUILD)/sdl/obj \
	    NDK_LIBS_OUT=$(BUILD)/sdl/lib APP_ABI=arm64-v8a APP_PLATFORM=android-$(MIN_SDK) APP_MODULES=SDL2 -j8 \
	    APP_CFLAGS="-ffile-prefix-map=$(abspath $(SDL_SRC))=SDL2 -ffile-prefix-map=$(abspath $(NDK))=ndk"
	@mkdir -p $(dir $@)
	cp $(BUILD)/sdl/lib/arm64-v8a/libSDL2.so $@

$(GAME_OBJS) $(PORT_OBJS): | $(SDL_SRC)/Android.mk

# SDL's Java side plus ours, for Java 8 (what d8 turns into Android code).
JAVA_SRCS = $(wildcard $(SDL_SRC)/android-project/app/src/main/java/org/libsdl/app/*.java) \
            $(shell find JVPPBPort/android/java -name '*.java')
$(BUILD)/apk/classes.dex: $(SDL_SRC)/Android.mk $(shell find JVPPBPort/android/java -name '*.java') | $(ANDROID_JAR)
	rm -rf $(BUILD)/classes && mkdir -p $(BUILD)/classes $(dir $@)
	javac -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath $(ANDROID_JAR):$(BUILD_TOOLS)/core-lambda-stubs.jar -d $(BUILD)/classes $(JAVA_SRCS)
	$(BUILD_TOOLS)/d8 --release --min-api $(MIN_SDK) --lib $(ANDROID_JAR) --output $(dir $@) \
	    $$(find $(BUILD)/classes -name '*.class')

# The launcher icon: the same ball logo as the Windows icon, as an
# Android "adaptive icon" (ball in front of a plain background).
ICON_SIZES := mdpi:108 hdpi:162 xhdpi:216 xxhdpi:324 xxxhdpi:432
$(BUILD)/iconres/.done: JVPPBPort/tools/makeicon.py
	@mkdir -p $(dir $@)
	$(foreach d,$(ICON_SIZES),mkdir -p $(BUILD)/iconres/mipmap-$(word 1,$(subst :, ,$(d))) && \
	    python3 JVPPBPort/tools/makeicon.py --android-foreground $(word 2,$(subst :, ,$(d))) \
	    $(BUILD)/iconres/mipmap-$(word 1,$(subst :, ,$(d)))/ic_launcher_fg.png &&) true
	touch $@

# Version code (Android only installs a higher one over an installed app):
# MAJOR*1000000 + MINOR*10000 + PATCH*100 from version.h, plus, for test
# builds, the next APK build number (JVPPBPort/tools/apk_version.txt, private;
# missing = 0).
PORT_VER := $(shell sed -n 's/^\#define PORT_VERSION "\(.*\)"/\1/p' JVPPBPort/include/version.h)
RELEASE_CODE := $(shell echo $(PORT_VER) | awk -F. '{print $$1*1000000 + $$2*10000 + $$3*100}')
ifeq ($(RELEASE),1)
VERSION_CODE := $(RELEASE_CODE)
VERSION_NAME := $(PORT_VER)
DEBUG_MODE   :=
else
APK_BUILD    := $(shell echo $$(( $$(cat JVPPBPort/tools/apk_version.txt 2>/dev/null || echo 0) + 1 )))
VERSION_CODE := $(shell echo $$(( $(RELEASE_CODE) + $(APK_BUILD) )))
VERSION_NAME := $(PORT_VER)-test.$(APK_BUILD)
DEBUG_MODE   := --debug-mode
endif
# Rewritten only when the version or release mode changes, so the .apk is
# repacked then.
APK_MODE := $(VERSION_CODE) $(VERSION_NAME) $(DEBUG_MODE)
$(BUILD)/apkmode.txt: FORCE
	@mkdir -p $(dir $@)
	@echo '$(APK_MODE)' | cmp -s - $@ || echo '$(APK_MODE)' > $@
.PHONY: FORCE
$(BUILD)/unsigned.apk: $(BUILD)/apkmode.txt JVPPBPort/android/AndroidManifest.xml $(shell find JVPPBPort/android/res -type f) \
                       $(BUILD)/iconres/.done $(BUILD)/apk/classes.dex $(APK_LIBS)/libmain.so $(APK_LIBS)/libSDL2.so \
                       JVPPBPort/tools/apkadd.py \
                       | $(BUILD_TOOLS)/aapt2
	rm -rf $(BUILD)/res && mkdir -p $(BUILD)/res
	$(BUILD_TOOLS)/aapt2 compile --dir JVPPBPort/android/res -o $(BUILD)/res/port.zip
	$(BUILD_TOOLS)/aapt2 compile --dir $(BUILD)/iconres -o $(BUILD)/res/icons.zip
	$(BUILD_TOOLS)/aapt2 link -o $(BUILD)/resources.apk -I $(ANDROID_JAR) --manifest JVPPBPort/android/AndroidManifest.xml $(DEBUG_MODE) \
	    --min-sdk-version $(MIN_SDK) --target-sdk-version $(TARGET_SDK) \
	    --version-code $(VERSION_CODE) --version-name "$(VERSION_NAME)" \
	    $(BUILD)/res/port.zip $(BUILD)/res/icons.zip
	python3 JVPPBPort/tools/apkadd.py $(BUILD)/resources.apk $@ $(BUILD)/apk classes.dex lib/arm64-v8a/libSDL2.so lib/arm64-v8a/libmain.so

$(BUILD)/gen/%.o: $(LINUX_GEN)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gen/%.o: $(LINUX_GEN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GAME_CPPFLAGS) -c $< -o $@

else

$(TARGET): $(LINK_OBJS) $(BUILD)/gen/build_id.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

ifneq ($(PREGEN),1)
# Data tables: converted from the GBA build's assembled data objects.
$(filter-out $(BUILD)/gen/ram_% $(BUILD)/gen/wrap_rows.c,$(GEN_SRCS)) &: $(DATA_OBJS) pokepinballrs.gba pokepinballrs.elf JVPPBPort/tools/data2c.py JVPPBPort/tools/elf32.py
	python3 JVPPBPort/tools/data2c.py $(BUILD)/gen $(DATA_OBJS)

$(BUILD)/gen/ram_syms.s $(BUILD)/gen/ram_aliases.c &: sym_ewram.txt sym_bss.txt sym_common.txt ld_script.txt include/main.h JVPPBPort/tools/syms2s.py
	@mkdir -p $(BUILD)/gen
	python3 JVPPBPort/tools/syms2s.py $(BUILD)/gen/ram_syms.s $(BUILD)/gen/ram_aliases.c

$(BUILD)/gen/wrap_rows.c: data/rom_2.s JVPPBPort/tools/wraprows.py
	@mkdir -p $(BUILD)/gen
	python3 JVPPBPort/tools/wraprows.py data/rom_2.s $@
endif

$(BUILD)/gen/%.o: $(GEN_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GAME_CPPFLAGS) -c $< -o $@

$(BUILD)/gen/%.o: $(GEN_SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

endif

# A fingerprint of the program (gPortBuildId): the table pictures made at
# the first start are retaken when it changes (see gamedata.c).
$(BUILD)/gen/build_id.o: $(LINK_OBJS)
	@mkdir -p $(dir $@)
	printf 'const char gPortBuildId[64] = "%s";\n' $$(cat $^ | sha1sum | cut -c1-40) | $(CC) -c -x c - -o $@

# Game C files go through pret's preproc (it converts the game's text strings
# with charmap.txt), the same way the GBA build does, then through
# portrewrite.py (small automatic edits for the static table view; see there).
$(BUILD)/game/%.o: src/%.c JVPPBPort/tools/portrewrite.py
	@mkdir -p $(dir $@)
	$(CC) -E $(CPPFLAGS) $(GAME_CPPFLAGS) -MMD -MP -MT $@ -MF $(BUILD)/game/$*.d $< -o $(BUILD)/game/$*.i
	$(PREPROC) $(BUILD)/game/$*.i charmap.txt > $(BUILD)/game/$*.pp0.c
	python3 JVPPBPort/tools/portrewrite.py $*.c $(BUILD)/game/$*.pp0.c $(BUILD)/game/$*.pp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -x c $(BUILD)/game/$*.pp.c -o $@

$(BUILD)/port/%.o: JVPPBPort/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Rebuild everything when the build settings change, and each file when a
# header it uses changes.
$(GAME_OBJS) $(PORT_OBJS) $(GEN_OBJS): JVPPBPort/Makefile
-include $(GAME_OBJS:.o=.d) $(PORT_OBJS:.o=.d)

clean:
	rm -rf $(BUILD)
