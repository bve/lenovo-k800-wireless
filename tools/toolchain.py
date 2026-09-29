"""Locate ARM GNU tools installed globally or by PlatformIO."""
import os
from pathlib import Path
import shutil


def arm_tool(name: str, directory: Path | None = None) -> str:
    executable = f"arm-none-eabi-{name}"
    if directory is not None:
        candidate = directory / executable
    elif found := shutil.which(executable):
        return found
    else:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
        candidate = core / "packages/toolchain-gccarmnoneeabi/bin" / executable
    if not candidate.is_file():
        raise FileNotFoundError(f"{executable} not found; build the PlatformIO environment first or provide --toolchain")
    return str(candidate)
