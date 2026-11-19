#!/usr/bin/env python3
"""Summarize buffered SDK Open graphic spans without counting children twice."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re


SPAN = re.compile(
    r"\[graphic_timing\] trace=(\d+) span=(\d+) parent=(\d+) "
    r"op=(\S+) stage=(\S+) input=(.*?) output=(.*?) "
    r"start_ms=([\d.]+) duration_ms=([\d.]+) self_ms=([\d.]+) "
    r"code=(\S+) exception=(\S+) dimensions=(\d+)x(\d+) channels=(\d+)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Runtime log or /api/logs response JSON")
    parser.add_argument("--trace", help="Show a specific trace")
    parser.add_argument("--last", type=int, default=1, help="Show the last N traces (default: 1)")
    args = parser.parse_args()
    content = args.log.read_text(encoding="utf-8", errors="replace")
    if content.lstrip().startswith("{"):
        content = json.loads(content).get("content", "")
    traces = defaultdict(list)
    for line in content.splitlines():
        match = SPAN.search(line)
        if not match:
            continue
        values = match.groups()
        traces[values[0]].append({
            "span": int(values[1]), "parent": int(values[2]),
            "op": values[3], "stage": values[4],
            "duration": float(values[8]), "self": float(values[9]),
            "code": values[10], "exception": values[11],
        })
    if args.last < 1:
        parser.error("--last must be positive")
    selected = [args.trace] if args.trace else list(traces)[-args.last:]
    if not selected or any(trace not in traces for trace in selected):
        parser.error("No matching graphic timing trace found")
    for trace in selected:
        spans = traces[trace]
        roots = [span for span in spans if span["parent"] == 0]
        if len(roots) != 1:
            print(f"trace={trace}: incomplete trace, expected one root; export a larger log tail")
            continue
        root = roots[0]
        print(f"\ntrace={trace} op={root['op']} total_ms={root['duration']:.3f} code={root['code']}")
        known_ids = {span["span"] for span in spans}
        summed = sum(span["self"] for span in spans)
        if len(known_ids) != len(spans) or any(
            span["parent"] and span["parent"] not in known_ids for span in spans
        ) or abs(summed - root["duration"]) > max(0.1, len(spans) * 0.002):
            print("WARNING: incomplete or duplicate spans; self-time totals may be inaccurate")
        costs = defaultdict(lambda: [0, 0.0])
        for span in spans:
            cost = costs[f"{span['op']}.{span['stage']}"]
            cost[0] += 1
            cost[1] += span["self"]
        print(f"{'stage (exclusive time)':<52} {'calls':>6} {'self_ms':>12} {'percent':>9}")
        for stage, (count, cost) in sorted(costs.items(), key=lambda item: item[1][1], reverse=True):
            percent = cost * 100.0 / root["duration"] if root["duration"] else 0.0
            print(f"{stage:<52} {count:>6} {cost:>12.3f} {percent:>8.1f}%")
        print(f"sum_self_ms={summed:.3f}")
        for span in spans:
            if span["stage"] in ("single_cut_pipeline", "cut_page_by_keypoints", "color_filter", "init_czcv"):
                print(f"algorithm={span['stage']} span={span['span']} duration_ms={span['duration']:.3f} exception={span['exception']}")


if __name__ == "__main__":
    main()
