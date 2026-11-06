# Builds Jonnisvibes PPB Port for PC. The game's data is not part of this
# repository: the program asks for your own ROM the first time it starts.
# See docs/BUILDING.md.
#
#   make          Linux version: build-JVPPBPort/jvppbport
#   make win      Windows version (MinGW-w64): build-JVPPBPort-win/JVPPBPort.exe
#   make release  the Windows version packed as release zips in release/
#   make clean    removes all of these

.PHONY: all linux win release preproc clean
all: linux

# pret's preproc converts the game's text strings (see JVPPBPort/Makefile).
preproc:
	$(MAKE) -C tools/preproc

linux: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1

win: preproc
	$(MAKE) -f JVPPBPort/Makefile PREGEN=1 WINDOWS=1

release: win
	python3 JVPPBPort/tools/releasezip.py release

clean:
	rm -rf build-JVPPBPort build-JVPPBPort-win release
	$(MAKE) -C tools/preproc clean
