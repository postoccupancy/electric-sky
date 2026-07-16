Import("env")

from datetime import datetime, timezone
import subprocess


project_dir = env.subst("$PROJECT_DIR")


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=project_dir, text=True
    ).strip()


try:
    git_sha = git("rev-parse", "--short=12", "HEAD")
    tracked_dirty = subprocess.call(
        ["git", "diff", "--quiet", "HEAD", "--"], cwd=project_dir
    ) != 0
except (OSError, subprocess.CalledProcessError):
    git_sha = "unknown"
    tracked_dirty = True

build_utc = datetime.now(timezone.utc).isoformat(timespec="seconds").replace(
    "+00:00", "Z"
)

env.Append(
    CPPDEFINES=[
        ("FW_GIT_SHA", env.StringifyMacro(git_sha)),
        ("FW_GIT_DIRTY", 1 if tracked_dirty else 0),
        ("FW_BUILD_UTC", env.StringifyMacro(build_utc)),
    ]
)
