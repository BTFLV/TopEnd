#!/usr/bin/env python3
"""Install only TopEnd to per-user plugin folders. Existing foreign bundles are preserved."""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--build", type=Path, default=Path("build-release/out"))
args = p.parse_args()
locations = {".component": "Components", ".vst3": "VST3", ".clap": "CLAP"}
for ext, folder in locations.items():
    source = args.build / f"TopEnd{ext}"
    if not source.is_dir():
        raise RuntimeError(f"Missing built bundle {source}")
    dest = Path.home() / "Library/Audio/Plug-Ins" / folder / source.name
    if dest.exists():
        try:
            bundle = plistlib.loads((dest / "Contents/Info.plist").read_bytes())
        except Exception as e:
            raise RuntimeError(f"Refusing to replace unrecognized {dest}") from e
        if (
            not bundle.get("CFBundleIdentifier", "")
            .lower()
            .startswith("audio.topendaudio.")
        ):
            raise RuntimeError(f"Refusing to replace foreign bundle {dest}")
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, dest)
    subprocess.run(["codesign", "--force", "--sign", "-", str(dest)], check=True)
    print(dest)
