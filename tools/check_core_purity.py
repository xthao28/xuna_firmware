"""Fail PlatformIO builds if the portable core imports hardware dependencies."""
from pathlib import Path
import re

Import("env")
root = Path(env.subst("$PROJECT_DIR")) / "lib" / "xuna_core"
include = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
violations = []
for path in sorted(root.rglob("*")):
    if not path.is_file() or path.suffix not in {".h", ".hpp", ".c", ".cpp", ".cc", ".inc"}:
        continue
    for match in include.finditer(path.read_text(encoding="utf-8")):
        name = match.group(1)
        base = name.rsplit("/", 1)[-1]
        if (base in {"Arduino.h", "Preferences.h"} or base.startswith(("esp_", "BLE", "TFT_eSPI"))
                or name.startswith(("freertos/", "soc/"))):
            line = path.read_text(encoding="utf-8")[:match.start()].count("\n") + 1
            violations.append(f"{path.relative_to(root)}:{line}: {name}")
if violations:
    raise RuntimeError("xuna_core purity check failed:\n" + "\n".join(violations))
print("xuna_core purity check: PASS")
