#!/usr/bin/env python3
"""Run isolated real-registry startup compatibility scenarios."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--executable", required=True)
parser.add_argument("--runner")
args = parser.parse_args()
command = ([args.runner] if args.runner else []) + [args.executable]
cases = [
    ("historical-input-negation", {}, ["--no_mouse_ui_nav", "--expect-mouse=false"], True),
    ("historical-noisy-cli", {}, ["--log_noisy=true"], True),
    ("historical-noisy-negation", {}, ["--no-log_noisy"], True),
    ("session-defaults", {}, [], True),
    ("legacy-cli", {}, ["--native_pipeline_cache=false", "--vulkan_shared_memory_upload_batching=false",
     "--log_max_files=3", "--log_max_file_size_bytes=8192", "--expect-bytes=8192", "--expect-files=3",
     "--expect-native=false", "--expect-batching=false"], True),
    ("legacy-environment", {"REX_NATIVE_PIPELINE_CACHE": "false",
     "REX_VULKAN_SHARED_MEMORY_UPLOAD_BATCHING": "false", "REX_LOG_MAX_FILES": "4",
     "REX_LOG_MAX_FILE_SIZE_BYTES": "32768"}, ["--expect-bytes=32768", "--expect-files=4",
     "--expect-native=false", "--expect-batching=false"], True),
    ("canonical-environment", {"REX_NATIVE_PIPELINE_CACHE": "false",
     "REX_VULKAN_SHARED_MEMORY_UPLOAD_BATCHING": "false", "REX_GPU_XENOS_NATIVE_PIPELINE_CACHE": "true",
     "REX_GPU_VULKAN_SHARED_MEMORY_UPLOAD_BATCHING": "true", "REX_LOG_MAX_FILES": "5",
     "REX_LOG_MAX_FILE_SIZE_BYTES": "65536"}, ["--expect-bytes=65536", "--expect-files=5"], True),
    ("legacy-mib-cli", {}, ["--log_max_file_size_mb=3", "--expect-bytes=3145728"], True),
    ("legacy-mib-environment", {"REX_LOG_MAX_FILE_SIZE_MB": "4"}, ["--expect-bytes=4194304"], True),
    ("canonical-byte-cli", {"REX_LOG_MAX_FILE_SIZE_MB": "4"},
     ["--log.max_file_size_bytes=4096", "--expect-bytes=4096"], True),
    ("canonical-byte-environment", {"REX_LOG_MAX_FILE_SIZE_MB": "4", "REX_LOG_MAX_FILE_SIZE_BYTES": "4096"},
     ["--expect-bytes=4096"], True),
    ("canonical-byte-same-cli-layer", {}, ["--log_max_file_size_mb=3", "--log.max_file_size_bytes=2048",
     "--expect-bytes=2048"], True),
    ("historical-200-mib-cli", {}, ["--log_max_file_size_mb=200", "--expect-bytes=209715200"], True),
    ("historical-200-mib-environment", {"REX_LOG_MAX_FILE_SIZE_MB": "200"},
     ["--expect-bytes=209715200"], True),
    ("historical-int32-max-mib", {}, ["--log_max_file_size_mb=2147483647",
     "--expect-bytes=2251799812636672"], True),
    ("canonical-byte-wins-over-200-mib", {}, ["--log_max_file_size_mb=200",
     "--log.max_file_size_bytes=4096", "--expect-bytes=4096"], True),
    ("canonical-byte-cli-wins-over-200-mib-environment", {"REX_LOG_MAX_FILE_SIZE_MB": "200"},
     ["--log.max_file_size_bytes=8192", "--expect-bytes=8192"], True),
    ("invalid-mib", {}, ["--log_max_file_size_mb=0"], False),
    ("negative-mib", {}, ["--log_max_file_size_mb=-1"], False),
    ("overflow-mib", {}, ["--log_max_file_size_mb=2147483648"], False),
]
temporary = tempfile.TemporaryDirectory(prefix="blitz-category-config-")
category_config = """[log.levels]
fs = "WARNING"
sys = "ERR"
krnl = "off"
gpu = "off"
apu = "invalid-legacy-level"
cpu = 17
legacy_fixture = "trace"
[log.categories]
gpu = "debug"
"""
def config_case(name, contents, environment=None, options=None, succeeds=True):
    file = Path(temporary.name) / (name+".toml")
    file.write_text(contents)
    cases.append((name, environment or {}, ["--fixture-config="+str(file), *(options or [])], succeeds))
config_case("legacy-category-file", category_config)
config_case("legacy-current-category-spelling", category_config.replace(
    'fs = "WARNING"', 'fs = "trace"\nfilesystem = "WARNING"'))
config_case("canonical-category-environment", category_config,
            {"REX_LOG_CATEGORIES_GPU":"error"}, ["--expect-gpu=error"])
config_case("canonical-category-cli", category_config,
            {"REX_LOG_CATEGORIES_GPU":"error"}, ["--log.categories.gpu=critical","--expect-gpu=critical"])
config_case("canonical-category-empty", category_config.replace('gpu = "debug"', 'gpu = ""'),
            options=["--expect-gpu="])
config_case("canonical-category-invalid", category_config+'apu = "invalid-canonical-level"\n', succeeds=False)
config_case("legacy-category-array", category_config+'\n[log.levels.bad]\nvalue = [1,2]\n', succeeds=False)
config_case("ordinary-config-array", category_config+'\n[unrelated]\nvalue = [1,2]\n', succeeds=False)
for name, environment, options, succeeds in cases:
    child_environment = {key: value for key, value in os.environ.items() if not key.startswith("REX_")}
    child_environment.update(environment)
    result = subprocess.run(command + options, env=child_environment, capture_output=True, text=True)
    expected_exit = 0 if succeeds else 2
    if result.returncode != expected_exit:
        raise SystemExit(f"{name}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}")
    if name == "legacy-category-array" and "use a string level under [log.categories]" not in result.stdout+result.stderr:
        raise SystemExit("Legacy array rejection did not explain the supported category syntax")
    print(f"{name}: passed", flush=True)
print(f"Passed {len(cases)} registry scenarios")
temporary.cleanup()
