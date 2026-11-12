#!/usr/bin/env python3
# JVPPBPort: adds files to an .apk made by aapt2 (Android build, see
# JVPPBPort/Makefile). .so libraries are stored uncompressed, so Android can
# load them straight from the .apk (zipalign then lines them up).
#
# Usage: apkadd.py IN.apk OUT.apk BASEDIR FILE...   (FILE relative to BASEDIR)
import os
import shutil
import sys
import zipfile


def main():
    src, out, base, files = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    shutil.copyfile(src, out + '.tmp')
    with zipfile.ZipFile(out + '.tmp', 'a') as z:
        for name in files:
            kind = zipfile.ZIP_STORED if name.endswith('.so') else zipfile.ZIP_DEFLATED
            z.write(os.path.join(base, name), name, compress_type=kind)
    os.replace(out + '.tmp', out)


if __name__ == '__main__':
    main()
