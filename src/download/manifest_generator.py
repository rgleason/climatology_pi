# MANIFEST GENERATOR PY
# How to run it.   ALSO DOES SHA
# python3 manifest_generator.py /path/to/climatology_pi_data > manifest.json

#!/usr/bin/env python3
#!/usr/bin/env python3
import os
import sys
import json
import hashlib

CHUNK_SIZE = 1024 * 1024  # 1 MB chunks for fast hashing

def sha256_of_file(path):
    """Compute SHA-256 checksum of a file in streaming mode."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def generate_manifest(data_dir):
    """
    Scan the climatology data directory and generate manifest entries.
    Includes:
      - windXX.gz
      - currentXX.gz
CHUNK_SIZE = 1024 * 1024  # 1 MB chunks for fast hashing

def sha256_of_file(path):
    ""
      - all scalar fields (*.gz)
      - cyclone-*.gz
      - elnino_years.txt.gz
    Computes SHA-256 checksums and file sizes.
    """

    entries = []

    # Normalize path
    data_dir = os.path.abspath(data_dir)

    # List all files in the directory
    for fname in sorted(os.listdir(data_dir)):
        # Only include .gz files
        if not fname.endswith(".gz"):
            continue

        fullpath = os.path.join(data_dir, fname)

        entry = {
            "filename": fname,
            "checksum": sha256_of_file(fullpath),
            "size": os.path.getsize(fullpath)
        }

        entries.append(entry)

    return entries


def main():
    if len(sys.argv) != 2:
        print("Usage: manifest_generator.py <climatology_data_directory>", file=sys.stderr)
        sys.exit(1)

    data_dir = sys.argv[1]

    if not os.path.isdir(data_dir):
        print(f"Error: {data_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    manifest = generate_manifest(data_dir)

    # Pretty-print JSON
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
