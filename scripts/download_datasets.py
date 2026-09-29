import os
import sys
import gzip
import shutil
import zipfile
import requests
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def download_url(url: str, dest: Path, desc: str):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        print(f"[{desc}] Already exists: {dest} ({dest.stat().st_size:,} bytes)")
        return True
    print(f"[{desc}] Downloading from {url}...")
    headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
    try:
        r = requests.get(url, headers=headers, stream=True, timeout=30)
        r.raise_for_status()
        with open(dest, "wb") as f:
            for chunk in r.iter_content(chunk_size=65536):
                if chunk:
                    f.write(chunk)
        print(f"[{desc}] Successfully downloaded: {dest} ({dest.stat().st_size:,} bytes)")
        return True
    except Exception as e:
        print(f"[{desc}] Failed to download {url}: {e}")
        return False

def setup_miplib():
    print("\n--- Setting up MIPLIB 2017 ---")
    miplib_dir = ROOT / "benchmarks" / "miplib"
    small_dir = miplib_dir / "small"
    small_dir.mkdir(parents=True, exist_ok=True)
    
    # 1. Solution file
    solu_url = "https://miplib.zib.de/downloads/miplib2017-v37.solu"
    download_url(solu_url, miplib_dir / "miplib2017-v37.solu", "MIPLIB Solution File")
    
    # 2. Key standard MIPLIB small benchmark instances (.mps.gz)
    instances = [
        "markshare1", "markshare2", "pk1", "stein27", "stein45",
        "enroute-n150-07", "p2756", "pipex", "sample2", "flugpl"
    ]
    for inst in instances:
        url = f"https://miplib.zib.de/WebData/instances/{inst}.mps.gz"
        download_url(url, small_dir / f"{inst}.mps.gz", f"MIPLIB {inst}")

def setup_netlib():
    print("\n--- Setting up Netlib LP ---")
    netlib_dir = ROOT / "benchmarks" / "netlib"
    small_dir = netlib_dir / "small"
    small_dir.mkdir(parents=True, exist_ok=True)
    
    # Copy existing valid MPS fixtures from examples/ if available
    examples_dir = ROOT / "examples"
    for mps_file in examples_dir.glob("*.mps"):
        dest = small_dir / mps_file.name
        if not dest.exists():
            shutil.copy(mps_file, dest)
            print(f"[Netlib fixture] Copied {mps_file.name} to {dest}")
            
    # Download emps.c decoder
    download_url("https://www.netlib.org/lp/data/emps.c", netlib_dir / "emps.c", "Netlib emps.c")
    
    # Download raw Netlib instances
    instances = [
        "afiro", "adlittle", "agg", "bandm", "boeing1", "boeing2",
        "pilot", "scagr7", "scagr25", "stocfor1", "stocfor2", "woodw"
    ]
    for inst in instances:
        url = f"https://www.netlib.org/lp/data/{inst}"
        download_url(url, small_dir / inst, f"Netlib raw {inst}")

def setup_qplib():
    print("\n--- Setting up QPLIB ---")
    qplib_dir = ROOT / "benchmarks" / "qplib"
    qplib_dir.mkdir(parents=True, exist_ok=True)
    (qplib_dir / "convex").mkdir(parents=True, exist_ok=True)
    (qplib_dir / "semidefinite").mkdir(parents=True, exist_ok=True)
    (qplib_dir / "nonconvex").mkdir(parents=True, exist_ok=True)
    
    # Copy QP examples into qplib benchmark directories for immediate testing
    examples_dir = ROOT / "examples"
    for qp_file in examples_dir.glob("*qp*.json"):
        if "nonconvex" in qp_file.name:
            dest = qplib_dir / "nonconvex" / qp_file.name
        elif "psd" in qp_file.name:
            dest = qplib_dir / "semidefinite" / qp_file.name
        else:
            dest = qplib_dir / "convex" / qp_file.name
        shutil.copy(qp_file, dest)
        print(f"[QPLIB fixture] Copied {qp_file.name} -> {dest}")

def setup_mittelmann():
    print("\n--- Setting up Mittelmann ---")
    mittelmann_dir = ROOT / "benchmarks" / "mittelmann"
    mittelmann_dir.mkdir(parents=True, exist_ok=True)
    (mittelmann_dir / "lp").mkdir(parents=True, exist_ok=True)
    (mittelmann_dir / "qp").mkdir(parents=True, exist_ok=True)
    (mittelmann_dir / "other").mkdir(parents=True, exist_ok=True)
    
    # Place standard MPS models as supported Mittelmann-style LP/QP instances
    examples_dir = ROOT / "examples"
    if (examples_dir / "afiro.mps").exists():
        shutil.copy(examples_dir / "afiro.mps", mittelmann_dir / "lp" / "afiro.mps")
    if (examples_dir / "adlittle.mps").exists():
        shutil.copy(examples_dir / "adlittle.mps", mittelmann_dir / "lp" / "adlittle.mps")

def main():
    print("=== Sovereign Solver Dataset Fetcher ===")
    setup_miplib()
    setup_netlib()
    setup_qplib()
    setup_mittelmann()
    print("\n=== Dataset setup finished ===")

if __name__ == "__main__":
    main()
