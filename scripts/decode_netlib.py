import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
netlib_dir = ROOT / "benchmarks" / "netlib"
small_dir = netlib_dir / "small"
emps_exe = netlib_dir / "emps.exe"

def main():
    print("=== Decoding Netlib EMPS Files with emps.exe ===")
    instances = [
        "afiro", "adlittle", "agg", "bandm", "boeing1", "boeing2",
        "pilot", "scagr7", "scagr25", "stocfor1", "stocfor2", "woodw"
    ]
    
    for inst in instances:
        raw_file = small_dir / inst
        if not raw_file.exists():
            print(f"Skipping {inst} (not found)")
            continue
        out_mps = small_dir / f"{inst}.mps"
        
        # Run emps.exe raw_file
        res = subprocess.run([str(emps_exe), str(raw_file)], capture_output=True, text=True)
        if res.returncode == 0 and res.stdout.strip():
            with open(out_mps, "w", encoding="utf-8") as f:
                f.write(res.stdout)
            print(f"Decoded {inst} -> {out_mps.name} ({len(res.stdout.splitlines())} lines, {out_mps.stat().st_size:,} bytes)")
        else:
            print(f"Failed decoding {inst}: {res.stderr}")

if __name__ == "__main__":
    main()
