# Builds Jonnisvibes PPB Port for PC and Android. The game's data is not part of this
# repository: the program asks for your own ROM the first time it starts.
# See docs/BUILDING.md.
#
#   make          Linux version: build-JVPPBPort/jvppbport
#   make win      Windows version (MinGW-w64): build-JVPPBPort-win/JVPPBPort.exe
#   make android  Android test version: build-JVPPBPort-android/JVPPBPort.apk
#   make release  the Windows zips and the Android release .apk in release/
#   make clean    removes all of these

.PHONY: all linux win android android-release release preproc clean
all: linux

# pret's preproc converts the game's text strings (see JVPPBPort/Makefile).
preproc:
	$(MAKE) -C tools/preproc

linux: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1

win: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1 WINDOWS=1

android: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1 ANDROID=1

android-release: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1 ANDROID=1 RELEASE=1

release: win android-release
	python3 JVPPBPort/tools/releasezip.py release

clean:
	rm -rf build-JVPPBPort build-JVPPBPort-win build-JVPPBPort-android release
	$(MAKE) -C tools/preproc clean
