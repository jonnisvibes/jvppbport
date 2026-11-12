#!/usr/bin/env python3
# JVPPBPort: packs the Windows release (run by `make release` in the public
# repository, after the Windows build). Writes into OUTDIR:
#   JVPPBPort-<version>-windows.zip        the .exe, README, licenses
#   JVPPBPort-<version>-windows-debug.zip  JVPPBPort.debug, to turn
#                                                   crash addresses into names
#   JVPPBPort-<version>-android.apk        the Android release .apk
#   JVPPBPort-<version>-android-debug.zip  libmain.debug.so, for Android crashes
#   SHA256SUMS.txt                         checksums of the zips and the .apk
# (The two -debug zips are for reading crash reports; keep them, they
# don't need to be uploaded.)
# The version is PORT_VERSION in JVPPBPort/include/version.h.
import hashlib, os, re, shutil, sys, zipfile

out = sys.argv[1] if len(sys.argv) > 1 else "release"
version = re.search(r'#define PORT_VERSION "([^"]+)"', open("JVPPBPort/include/version.h").read()).group(1)
base = "JVPPBPort-%s-windows" % version
build = "build-JVPPBPort-win"

def pack(name, files):
    path = os.path.join(out, name + ".zip")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, dst in files:
            z.write(src, name + "/" + dst)
    return path

os.makedirs(out, exist_ok=True)
zips = [
    pack(base, [(build + "/JVPPBPort.exe", "JVPPBPort.exe"),
                ("README.md", "README.md"),
                ("CHANGELOG.md", "CHANGELOG.md"),
                ("LICENSE.md", "LICENSE.md"),
                ("LICENSE-CC0.txt", "LICENSE-CC0.txt"),
                ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md")]),
    pack(base + "-debug", [(build + "/JVPPBPort.debug", "JVPPBPort.debug")]),
]
abase = "JVPPBPort-%s-android" % version
apk = os.path.join(out, abase + ".apk")
shutil.copyfile("build-JVPPBPort-android/JVPPBPort.apk", apk)
zips += [apk, pack(abase + "-debug", [("build-JVPPBPort-android/libmain.debug.so", "libmain.debug.so")])]
with open(os.path.join(out, "SHA256SUMS.txt"), "w") as f:
    for z in zips:
        f.write("%s  %s\n" % (hashlib.sha256(open(z, "rb").read()).hexdigest(), os.path.basename(z)))
for z in zips:
    print("%s (%d bytes)" % (z, os.path.getsize(z)))
print(open(os.path.join(out, "SHA256SUMS.txt")).read(), end="")
