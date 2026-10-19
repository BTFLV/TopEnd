#!/usr/bin/env python3
"""Fetch pinned open-source validators into the ignored local build directory."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/tools"
ASSETS = {
    "pluginval.zip": (
        "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip",
        "3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f",
    ),
    "clap-validator.zip": (
        "https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-macos-15-aarch64.zip",
        "719a0248ea431718bb7c92c8a0b9a78afa0bb23d692cf452e80973fe8b89282d",
    ),
}


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, (url, expected) in ASSETS.items():
        archive = OUTPUT / name
        data = (
            archive.read_bytes()
            if archive.exists()
            else urllib.request.urlopen(url, timeout=120).read()
        )
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"Unexpected archive checksum: {name}")
        if not archive.exists():
            archive.write_bytes(data)
        if name == "pluginval.zip":
            subprocess.run(["ditto", "-x", "-k", str(archive), str(OUTPUT)], check=True)
        else:
            with zipfile.ZipFile(io.BytesIO(data)) as outer:
                compressed = outer.read(outer.namelist()[0])
            with tarfile.open(fileobj=io.BytesIO(compressed), mode="r:gz") as inner:
                member = next(
                    m
                    for m in inner.getmembers()
                    if Path(m.name).name == "clap-validator" and m.isfile()
                )
                executable = OUTPUT / "clap-validator"
                executable.write_bytes(inner.extractfile(member).read())
                executable.chmod(0o755)
        print(f"Checked and extracted {name}")


if __name__ == "__main__":
    main()
