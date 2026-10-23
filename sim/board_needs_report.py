#!/usr/bin/env python3
"""Summarise a board Serial log for the life-feel board trial.

Reads the `needs ...` (every 5 s) and `activity_change ...` lines printed by
main.cpp and reports, per boot: first rest, awake/rest cycle lengths, energy
range and time shares. Compare with docs/roadmap/learning/life-lab-needs-timeline-2026-10-15.md
(first rest ~48 min, then ~25 min awake / ~4 min resting, energy 25-64%).

Example:
  python sim/board_needs_report.py ~/xuna_backups/<dir>/board-trial.log
"""
import argparse
import re
from pathlib import Path

FIELDS = re.compile(r"(\w+)=(\S+)")
LOW_ENERGY = 24576  # 37.5 % of 65535
REST = ("resting", "sleeping")


def parse(path):
    """Yield (kind, fields) for needs/activity_change lines, split into boots."""
    boots, current, last_t = [], [], -1
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if "needs t_s=" in line:
            kind = "needs"
        elif "activity_change from=" in line:
            kind = "change"
        else:
            continue
        fields = dict(FIELDS.findall(line))
        t = int(fields["t_s"])
        if t < last_t and current:  # uptime went back: the board rebooted
            boots.append(current)
            current = []
        last_t = t
        current.append((kind, fields))
    if current:
        boots.append(current)
    return boots


def minutes(seconds):
    return f"{seconds / 60:.1f} min"


def summarise(index, events):
    samples = [f for k, f in events if k == "needs"]
    changes = [f for k, f in events if k == "change"]
    end = int(events[-1][1]["t_s"])
    print(f"Boot {index + 1}: {minutes(end)} of uptime, {len(samples)} needs samples, {len(changes)} activity changes")
    if not samples:
        return
    rest_starts = [int(f["t_s"]) for f in changes if f["activity"] in REST and f["from"] not in REST]
    rest_ends = [int(f["t_s"]) for f in changes if f["from"] in REST and f["activity"] not in REST]
    print(f"  first rest: {minutes(rest_starts[0]) if rest_starts else 'none'}")
    rests = []
    for start in rest_starts:
        later = [e for e in rest_ends if e > start]
        if later:
            rests.append(later[0] - start)
    awakes = []
    for stop in rest_ends:
        later = [s for s in rest_starts if s > stop]
        if later:
            awakes.append(later[0] - stop)
    if rests:
        print(f"  rest periods: n={len(rests)} mean {minutes(sum(rests) / len(rests))} "
              f"(min {minutes(min(rests))}, max {minutes(max(rests))})")
    if awakes:
        print(f"  awake periods between rests: n={len(awakes)} mean {minutes(sum(awakes) / len(awakes))} "
              f"(min {minutes(min(awakes))}, max {minutes(max(awakes))})")
    sleeping = [f for f in changes if f["activity"] == "sleeping"]
    print(f"  entered sleeping: {len(sleeping)} times")
    after = [f for f in samples if rest_starts and int(f["t_s"]) >= rest_starts[0]] or samples
    energy = [int(f["energy"]) for f in after]
    label = "after first rest" if rest_starts else "whole boot"
    print(f"  energy {label}: {min(energy) * 100 / 65535:.1f}% - {max(energy) * 100 / 65535:.1f}%, "
          f"below 37.5% in {sum(e < LOW_ENERGY for e in energy) * 100 / len(energy):.1f}% of samples")
    resting = sum(f["activity"] in REST for f in samples) * 100 / len(samples)
    stim = max(int(f["stim"]) for f in samples) * 100 / 65535
    print(f"  resting + sleeping: {resting:.1f}% of samples; max stimulation {stim:.1f}%")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    boots = parse(args.log)
    if not boots:
        print("No needs/activity_change lines found.")
    for index, events in enumerate(boots):
        summarise(index, events)


if __name__ == "__main__":
    main()
