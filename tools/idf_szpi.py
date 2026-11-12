"""Run this board's firmware with the registered ESP-IDF 6.0.2 installation."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    registry = Path(os.environ.get("IDF_EIM_REGISTRY", "C:/Espressif/tools/eim_idf.json"))
    config = json.loads(registry.read_text(encoding="utf-8"))
    installation = next(
        item for item in config["idfInstalled"] if item["name"] == "v6.0.2"
    )
    environment = os.environ.copy()
    profile = Path(installation["activationScript"]).read_text(encoding="utf-8-sig")
    # EIM records concrete tool paths in its activation script. Read the values
    # directly so this entry point also works when PowerShell cannot start.
    for name, value in re.findall(r'^    "(\w+)" = "([^"]*)"', profile, re.MULTILINE):
        environment[name] = value
    tool_path = re.search(r'"PATH=([^"\r\n]*)"', profile)
    if tool_path is None:
        raise RuntimeError("EIM activation script has no exported PATH")
    environment["PATH"] = tool_path[1] + os.pathsep + environment.get("PATH", "")
    environment["ESP_IDF_VERSION"] = "6.0"
    environment["PYTHONUTF8"] = "1"
    python = installation["python"]
    idf_path = Path(installation["path"])
    version = subprocess.check_output(
        [python, str(idf_path / "tools/idf.py"), "--version"],
        env=environment,
        text=True,
    ).strip()
    if version != "ESP-IDF v6.0.2":
        raise RuntimeError(f"Expected ESP-IDF v6.0.2, found {version}")
    actions = sys.argv[1:] or ["build"]
    key = ROOT / "firmware/keys/ota_signing_key.pem"
    if any(action in actions for action in ("build", "all", "app", "flash", "app-flash")) and not key.exists():
        key.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [python, "-m", "espsecure", "generate-signing-key", "--version", "2",
             "--scheme", "rsa3072", str(key)],
            env=environment,
            check=True,
        )
    command = [
        python, str(idf_path / "tools/idf.py"),
        "-C", str(ROOT / "firmware"),
        "-B", str(ROOT / "firmware/build-szpi"),
        "-D", "SDKCONFIG=sdkconfig.szpi.local",
        "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.szpi",
        "-D", "PT_LCD_TEST=OFF",
        *actions,
    ]
    return subprocess.call(command, cwd=ROOT, env=environment)


if __name__ == "__main__":
    sys.exit(main())
