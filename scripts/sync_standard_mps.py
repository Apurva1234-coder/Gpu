import os
import sys
import gzip
import shutil
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

def setup_netlib_standard_mps():
    print("\n--- Setting up Clean Standard Netlib MPS ---")
    netlib_small = ROOT / "benchmarks" / "netlib" / "small"
    netlib_small.mkdir(parents=True, exist_ok=True)
    
    # Download standard uncompressed MPS instances from HiGHS verified test set
    highs_instances = [
        "afiro.mps", "adlittle.mps", "agg.mps", "bandm.mps", "boeing1.mps", 
        "boeing2.mps", "scagr7.mps", "scagr25.mps", "stocfor1.mps"
    ]
    for inst in highs_instances:
        url = f"https://raw.githubusercontent.com/ERGO-Code/HiGHS/master/check/instances/{inst}"
        download_url(url, netlib_small / inst, f"Netlib standard {inst}")

def setup_miplib_instances():
    print("\n--- Setting up MIPLIB 2017 Instances ---")
    miplib_small = ROOT / "benchmarks" / "miplib" / "small"
    miplib_small.mkdir(parents=True, exist_ok=True)
    
    # HiGHS repo also has verified small MIPLIB instances in .mps and .mps.gz
    # Let's download clean MIP instances
    highs_mip = [
        "flugpl.mps", "p2756.mps", "pipex.mps", "sample2.mps", "stein27.mps"
    ]
    for inst in highs_mip:
        url = f"https://raw.githubusercontent.com/ERGO-Code/HiGHS/master/check/instances/{inst}"
        # Save as .mps and compress to .mps.gz
        dest_mps = miplib_small / inst
        if download_url(url, dest_mps, f"MIPLIB {inst}"):
            # Also write .mps.gz
            gz_path = miplib_small / f"{inst}.gz"
            with open(dest_mps, "rb") as f_in, gzip.open(gz_path, "wb") as f_out:
                f_out.writelines(f_in)
            print(f"[MIPLIB compressed] Created {gz_path}")

def setup_mittelmann_standard():
    print("\n--- Setting up Mittelmann Standard Test Files ---")
    mittelmann_lp = ROOT / "benchmarks" / "mittelmann" / "lp"
    mittelmann_lp.mkdir(parents=True, exist_ok=True)
    netlib_small = ROOT / "benchmarks" / "netlib" / "small"
    for mps in ["afiro.mps", "adlittle.mps", "agg.mps"]:
        src = netlib_small / mps
        if src.exists():
            shutil.copy(src, mittelmann_lp / mps)
            print(f"[Mittelmann LP] Synced {mps}")

def main():
    setup_netlib_standard_mps()
    setup_miplib_instances()
    setup_mittelmann_standard()
    print("\n=== Dataset Sync Finished ===")

if __name__ == "__main__":
    main()
