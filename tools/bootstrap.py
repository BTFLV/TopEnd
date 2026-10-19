#!/usr/bin/env python3
"""Connect pinned SDK trees to iPlug2's expected layout without downloads or globals."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
subprocess.run(
    ["git", "submodule", "update", "--init", "--recursive"], cwd=root, check=True
)
deps = root / "external/iPlug2/Dependencies/IPlug"
for name, src in [
    ("VST3_SDK", "vst3sdk"),
    ("CLAP_SDK", "clap"),
    ("CLAP_HELPERS", "clap-helpers"),
]:
    dest = deps / name
    dest.mkdir(exist_ok=True)
    for child in (root / "external" / src).iterdir():
        if child.name.startswith(".") or child.name.lower() in (
            "readme.md",
            "readme.txt",
        ):
            continue
        target = dest / child.name
        if target.is_symlink():
            if target.resolve() != child.resolve():
                raise RuntimeError(f"Unexpected existing link: {target}")
        elif target.exists():
            raise RuntimeError(f"Refusing to replace existing SDK file: {target}")
        else:
            target.symlink_to(
                Path("../../../../") / src / child.name,
                target_is_directory=child.is_dir(),
            )
print("Pinned SDKs connected to iPlug2")
