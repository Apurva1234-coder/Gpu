import os
import sys
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def find_compiler():
    # Check standard paths and winget installed paths
    candidates = [
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "WinLibs" / "bin" / "g++.exe",
        Path("C:/Program Files/WinLibs/bin/g++.exe"),
        Path("C:/winlibs/bin/g++.exe"),
        Path("C:/msys64/ucrt64/bin/g++.exe"),
        Path("C:/msys64/mingw64/bin/g++.exe"),
    ]
    for c in candidates:
        if c.exists():
            return c
    # Check PATH
    gpp = shutil.which("g++")
    if gpp:
        return Path(gpp)
    # Check for any g++.exe on system
    for p in Path(os.environ.get("LOCALAPPDATA", "")).glob("**/g++.exe"):
        return p
    return None

def main():
    compiler = find_compiler()
    print(f"Compiler located: {compiler}")

if __name__ == "__main__":
    main()
