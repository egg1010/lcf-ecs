#!/usr/bin/env python3
"""运行 test_perf_* 并检查吞吐有效性与回归。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

METRIC_RE = re.compile(r"^\s*(.+?)\s*:\s*([\d.]+)\s+(?:Mops/s|MB/s)\b")
# 已知无效基准项
ALLOW_ZERO = {
    "mask_block/offset_of",
    "mask_type_of",
    "pin+stealable",
}


def perf_binaries(build_dir: Path) -> list[Path]:
    patterns = ("test_perf_*.exe", "test_perf_*")
    found: list[Path] = []
    for pattern in patterns:
        for path in sorted(build_dir.glob(pattern)):
            if path.is_file() and path not in found:
                found.append(path)
    return found


def run_one(binary: Path) -> tuple[dict[str, float], list[str]]:
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    errors: list[str] = []
    if result.returncode != 0:
        errors.append(f"{binary.name}: exit={result.returncode}")
    metrics: dict[str, float] = {}
    for line in result.stdout.splitlines():
        match = METRIC_RE.match(line)
        if not match:
            continue
        name = match.group(1).strip()
        metrics[name] = float(match.group(2))
    if not metrics:
        errors.append(f"{binary.name}: no Mops/s metrics")
    return metrics, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--baseline")
    parser.add_argument("--save-baseline")
    parser.add_argument("--min-ratio", type=float, default=0.70)
    parser.add_argument("--max-invalid", type=int, default=8)
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    binaries = perf_binaries(build_dir)
    if not binaries:
        print(f"perf gate: no perf binaries under {build_dir}", file=sys.stderr)
        return 2

    current: dict[str, float] = {}
    errors: list[str] = []
    invalid: list[str] = []
    for binary in binaries:
        metrics, run_errors = run_one(binary)
        errors.extend(run_errors)
        for name, value in metrics.items():
            key = f"{binary.stem}::{name}"
            if value <= 0.0 and name not in ALLOW_ZERO:
                invalid.append(f"{key}: invalid throughput {value}")
            current[key] = value
    if len(invalid) > args.max_invalid:
        errors.extend(invalid)
    elif invalid:
        for item in invalid:
            print(f"WARN: {item}", file=sys.stderr)

    if args.save_baseline:
        Path(args.save_baseline).write_text(
            json.dumps(current, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    if args.baseline:
        baseline_path = Path(args.baseline)
        if baseline_path.exists():
            baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
            for key, old_value in baseline.items():
                if old_value <= 0.0 or key not in current:
                    continue
                ratio = current[key] / old_value
                if ratio < args.min_ratio:
                    errors.append(
                        f"{key}: regression {ratio:.2f}x "
                        f"(current={current[key]:.3f}, baseline={old_value:.3f})"
                    )

    print(f"perf gate: {len(binaries)} binaries, {len(current)} metrics")
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("perf gate: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
