"""Normalize source and installed-package paths embedded by GCC."""

Import("env")

import os
import subprocess
from pathlib import Path


project_dir = os.path.abspath(env.subst("$PROJECT_DIR"))
core_dir = os.path.abspath(env.subst("$PROJECT_CORE_DIR"))
source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
if not source_date_epoch:
    source_date_epoch = subprocess.check_output(
        ["git", "-C", project_dir, "log", "-1", "--format=%ct"],
        text=True).strip()
if not source_date_epoch.isascii() or not source_date_epoch.isdecimal():
    raise RuntimeError("SOURCE_DATE_EPOCH must be Unix seconds since 1970-01-01")

flags = [f"-ffile-prefix-map={project_dir}=."]
packages_dir = Path(core_dir) / "packages"

if packages_dir.is_dir():
    for package_dir in sorted(packages_dir.iterdir()):
        if not package_dir.is_dir():
            continue
        # Some Core installs append a content hash to source package names.
        # Map those to the same stable package label as regular installs.
        package_name = package_dir.name.split("@src-", 1)[0]
        flags.append(
            f"-ffile-prefix-map={package_dir}=platform-packages/{package_name}")

env.Append(CCFLAGS=flags)
env.Append(ENV={"SOURCE_DATE_EPOCH": source_date_epoch})
# Make the epoch part of SCons' compile signature so changing the selected
# source timestamp rebuilds objects containing __DATE__ or __TIME__.
env.Append(CPPDEFINES=[("ESPHERM_BUILD_EPOCH", source_date_epoch)])
