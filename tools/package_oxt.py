#!/usr/bin/env python3
"""Package the MonteCalc extension (.oxt) for one platform.

Usage: package_oxt.py <platform> <library> <montecalc.rdb> <output.oxt>
  platform: linux_x86_64 | windows_x86_64 | macos_x86_64 | macos_aarch64
  library:  the built component library (libmontecalc.so, montecalc.dll, libmontecalc.dylib)

Copies ext/, rewrites the platform-specific entries of description.xml,
META-INF/manifest.xml and montecalc.components, adds the library and the type
library, and zips everything (Python only, so it works the same on Windows).
"""
import os
import re
import sys
import zipfile

# platform: (description.xml value, manifest platform, components platform)
PLATFORMS = {
    "linux_x86_64": ("linux_x86_64", "Linux_x86_64"),
    "windows_x86_64": ("windows_x86_64", "Windows_x86_64"),
    "macos_x86_64": ("macos_x86_64", "MacOSX_x86_64"),
    "macos_aarch64": ("macos_aarch64", "MacOSX_aarch64"),
}


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in PLATFORMS:
        sys.exit(__doc__)
    platform, lib, rdb, out = sys.argv[1:]
    desc_value, uno_platform = PLATFORMS[platform]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ext")
    libname = os.path.basename(lib)
    files = {}
    for base, _, names in os.walk(root):
        for n in names:
            full = os.path.join(base, n)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            with open(full, "rb") as f:
                files[rel] = f.read()
    files["description.xml"] = re.sub(rb'<platform value="[^"]*"/>', f'<platform value="{desc_value}"/>'.encode(), files["description.xml"])
    files["META-INF/manifest.xml"] = re.sub(rb"platform=[A-Za-z0-9_]+", f"platform={uno_platform}".encode(), files["META-INF/manifest.xml"])
    files["montecalc.components"] = re.sub(rb'uri="[^"]*"', f'uri="{libname}"'.encode(), files["montecalc.components"])
    with open(lib, "rb") as f:
        files[libname] = f.read()
    with open(rdb, "rb") as f:
        files["montecalc.rdb"] = f.read()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):
            z.writestr(name, files[name])
    print(f"packaged {out} ({platform}, {len(files)} files)")


if __name__ == "__main__":
    main()
