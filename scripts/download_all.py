import os
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def download_file(url: str, dest: Path, desc: str):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        print(f"[{desc}] Already exists: {dest} ({dest.stat().st_size} bytes)")
        return
    print(f"[{desc}] Downloading {url} -> {dest}...")
    try:
        urllib.request.urlretrieve(url, dest)
        print(f"[{desc}] Done: {dest} ({dest.stat().st_size} bytes)")
    except Exception as e:
        print(f"[{desc}] Error downloading {url}: {e}")

def main():
    print("=== Downloading Datasets ===")
    
    # 1. MIPLIB 2017
    miplib_dir = ROOT / "benchmarks" / "miplib"
    miplib_small = miplib_dir / "small"
    miplib_small.mkdir(parents=True, exist_ok=True)
    
    # Solution file
    solu_url = "https://miplib.zib.de/downloads/miplib2017-v37.solu"
    download_file(solu_url, miplib_dir / "miplib2017-v37.solu", "MIPLIB Solution File")
    
    # Key MIPLIB instances (.mps.gz)
    miplib_instances = [
        "markshare1", "markshare2", "pk1", "stein27", "stein45", 
        "enroute-n150-07", "p2756", "pipex", "sample2", "flugpl"
    ]
    for inst in miplib_instances:
        inst_url = f"https://miplib.zib.de/WebData/instances/{inst}.mps.gz"
        download_file(inst_url, miplib_small / f"{inst}.mps.gz", f"MIPLIB {inst}")

    # 2. Netlib LP
    netlib_dir = ROOT / "benchmarks" / "netlib"
    netlib_small = netlib_dir / "small"
    netlib_small.mkdir(parents=True, exist_ok=True)
    
    # Download emps.c decoder
    emps_url = "https://www.netlib.org/lp/data/emps.c"
    download_file(emps_url, netlib_dir / "emps.c", "Netlib emps.c")
    
    # Raw Netlib files
    netlib_instances = [
        "afiro", "adlittle", "agg", "bandm", "boeing1", "boeing2",
        "pilot", "scagr7", "scagr25", "stocfor1", "stocfor2", "woodw"
    ]
    for inst in netlib_instances:
        inst_url = f"https://www.netlib.org/lp/data/{inst}"
        download_file(inst_url, netlib_small / inst, f"Netlib {inst}")

    # 3. QPLIB
    qplib_dir = ROOT / "benchmarks" / "qplib"
    qplib_dir.mkdir(parents=True, exist_ok=True)
    # Key QPLIB continuous convex instances or small sample
    qplib_sample = qplib_dir / "small"
    qplib_sample.mkdir(parents=True, exist_ok=True)
    
    print("=== Dataset download complete ===")

if __name__ == "__main__":
    main()
