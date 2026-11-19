#!/usr/bin/env python3
"""Summarize opt-in video diagnostics; submission rate is not display FPS."""

import argparse
from collections import Counter
import json
import math
import re


RECORD = re.compile(
    r"\[video-debug\] ([\w-]+) a=(-?\d+) b=(-?\d+) c=(-?\d+) d=(-?\d+) "
    r"backlog=(\d+) frame_delay=(-?\d+) slow=(-?\d+)\s*$"
)
STAGES = {
    "worker-convert-time": "convert",
    "worker-copy-time": "copy",
    "worker-render-time": "render",
    "render-draw-time": "output",
}


def distribution(values):
    ordered = sorted(values)
    count = len(ordered)
    return {
        "count": count,
        "mean": sum(ordered) / count if count else None,
        **{f"p{p}": ordered[math.ceil(p * count / 100) - 1] if count else None
           for p in (50, 95, 99)},
        "max": ordered[-1] if count else None,
    }


def submission_summary(intervals):
    return {
        "submission_interval_us": distribution(intervals),
        "submission_fps": len(intervals) * 1e6 / sum(intervals) if len(intervals) >= 2 else None,
    }


def summarize(lines):
    events = Counter()
    timings = {name: [] for name in (
        "decode", "legacy_decode_and_convert", "convert", "copy", "render", "output",
    )}
    copy_bytes = []
    intervals = []
    runs = []
    previous = None
    empty_queue_retries = 0
    for number, line in enumerate(lines, 1):
        if "[video-debug]" not in line:
            continue
        match = RECORD.search(line)
        if not match:
            raise ValueError(f"line {number}: malformed video diagnostic")
        event = match[1]
        a, b, c = (int(match[index]) for index in (2, 3, 4))
        events[event] += 1
        stage = STAGES.get(event)
        if event == "worker-decode-time":
            stage = "decode" if c == 1 else "legacy_decode_and_convert"
        if stage:
            if b < 0:
                raise ValueError(f"line {number}: negative duration")
            timings[stage].append(b)
        if event == "worker-copy-time":
            if c < 0:
                raise ValueError(f"line {number}: negative copy size")
            copy_bytes.append(c)
        if event in ("play-start", "tick-stop", "render-output-error"):
            if previous is not None:
                runs.append(intervals)
                intervals = []
            previous = None
        if event == "render-wait" and b == -1:
            empty_queue_retries += 1
        if event == "render-submit":
            if b < 0 or (previous and b <= previous[1]):
                raise ValueError(f"line {number}: invalid submission clock")
            if previous and a > previous[0]:
                intervals.append(b - previous[1])
            elif previous:
                runs.append(intervals)
                intervals = []
            previous = (a, b)
    if not events:
        raise ValueError("no video diagnostics found; enable PIXELTERM_DEBUG_VIDEO")
    if previous is not None:
        runs.append(intervals)
    return {
        "events": dict(sorted(events.items())),
        "timings_us": {name: distribution(values) for name, values in timings.items()},
        "copy_bytes": distribution(copy_bytes),
        **submission_summary(runs[0] if len(runs) == 1 else []),
        "submission_runs": [submission_summary(run) for run in runs],
        "empty_queue_retries": empty_queue_retries,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="one uninterrupted playback run, without pause/seek")
    args = parser.parse_args()
    try:
        with open(args.log, encoding="utf-8") as stream:
            result = summarize(stream)
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
