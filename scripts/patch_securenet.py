Import("env")

import subprocess
from pathlib import Path


project_dir = Path(env["PROJECT_DIR"])
sdk_dir = project_dir / "freeink-sdk"
patch_file = project_dir / "patches" / "x4pro-network-throughput.patch"


def git_apply(*args):
    return subprocess.run(
        ["git", "apply", *args, str(patch_file)],
        cwd=sdk_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


if git_apply("--reverse", "--check").returncode == 0:
    print("SecureNet X4 Pro throughput patch already applied")
else:
    check = git_apply("--check")
    if check.returncode != 0:
        raise RuntimeError(f"SecureNet throughput patch no longer applies: {check.stderr.strip()}")
    applied = git_apply()
    if applied.returncode != 0:
        raise RuntimeError(f"Failed to apply SecureNet throughput patch: {applied.stderr.strip()}")
    print("Applied SecureNet X4 Pro throughput patch")
