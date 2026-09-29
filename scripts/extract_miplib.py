import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
zip_path = ROOT / "benchmarks" / "miplib" / "benchmark.zip"
small_dir = ROOT / "benchmarks" / "miplib" / "small"

def main():
    if not zip_path.exists():
        print(f"Error: {zip_path} not found.")
        return
    
    print(f"Extracting {zip_path} ({zip_path.stat().st_size:,} bytes)...")
    with zipfile.ZipFile(zip_path, 'r') as zf:
        members = zf.namelist()
        print(f"Total instances in benchmark.zip: {len(members)}")
        # Extract all .mps.gz files into small_dir
        for member in members:
            if member.endswith(".mps.gz") or member.endswith(".mps"):
                zf.extract(member, small_dir)
    print(f"Extraction complete. Total instances in {small_dir}: {len(list(small_dir.glob('*.mps.gz')))}")

if __name__ == "__main__":
    main()
