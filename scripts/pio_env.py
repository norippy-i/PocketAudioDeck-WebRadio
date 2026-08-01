from pathlib import Path
import sys

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
local_deps = project_dir / ".pio-python-deps"

if local_deps.exists():
    if str(local_deps) not in sys.path:
        sys.path.insert(0, str(local_deps))
    env["ENV"]["PYTHONPATH"] = str(local_deps)
    env["ENV"]["PYTHONNOUSERSITE"] = "1"
    print(f"[pio_env] PYTHONPATH={local_deps}")
else:
    print("[pio_env] using PlatformIO Python environment")
