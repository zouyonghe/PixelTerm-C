import io
import unittest

from summarize_video_log import summarize


def record(event, a=0, b=0, c=0):
    return (f"[video-debug] {event} a={a} b={b} c={c} d=0 "
            "backlog=0 frame_delay=33 slow=0\n")


class VideoLogSummaryTests(unittest.TestCase):
    def test_stage_percentiles_use_nearest_rank(self):
        log = "unrelated output\n" + "".join(
            record("worker-convert-time", b=value) for value in range(1, 101)
        )
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["timings_us"]["convert"], {
            "count": 100, "mean": 50.5, "p50": 50,
            "p95": 95, "p99": 99, "max": 100,
        })
        self.assertIsNone(summary["timings_us"]["decode"]["mean"])

    def test_old_decode_samples_are_not_mixed_with_decode_only(self):
        log = record("worker-decode-time", b=90)
        log += record("worker-decode-time", b=30, c=1)
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["timings_us"]["decode"]["mean"], 30)
        self.assertEqual(summary["timings_us"]["legacy_decode_and_convert"]["mean"], 90)

    def test_copy_timing_and_bytes_are_summarized(self):
        log = record("worker-copy-time", b=75, c=8294400)
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["timings_us"]["copy"]["mean"], 75)
        self.assertEqual(summary["copy_bytes"]["count"], 1)
        self.assertEqual(summary["copy_bytes"]["mean"], 8294400)

    def test_submission_intervals_counts_and_empty_queue_retries(self):
        log = record("render-submit", a=0, b=1000000)
        log += record("render-submit", a=33, b=1030000)
        log += record("render-submit", a=66, b=1070000)
        log += record("worker-drop-late") + record("worker-drop-stale")
        log += record("render-wait", b=-1) + record("render-wait", b=90)
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["submission_interval_us"]["p95"], 40000)
        self.assertAlmostEqual(summary["submission_fps"], 2e6 / 70000)
        self.assertEqual(summary["events"]["worker-drop-late"], 1)
        self.assertEqual(summary["empty_queue_retries"], 1)

    def test_single_sample_and_legacy_logs_do_not_invent_fps(self):
        for log in (record("render-submit", b=1000), record("render-frame", a=33)):
            with self.subTest(log=log):
                summary = summarize(io.StringIO(log))
                self.assertIsNone(summary["submission_fps"])
                self.assertEqual(summary["submission_interval_us"]["count"], 0)

    def test_single_interval_keeps_distribution_without_fps(self):
        log = record("render-submit", a=0, b=1000) + record("render-submit", a=33, b=34000)
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["submission_interval_us"]["count"], 1)
        self.assertEqual(summary["submission_interval_us"]["p50"], 33000)
        self.assertIsNone(summary["submission_fps"])

    def test_submission_intervals_reset_on_restart_or_backward_pts(self):
        log = record("render-submit", a=100, b=1000)
        log += record("play-start")
        log += record("render-submit", a=200, b=1000000)
        log += record("render-submit", a=0, b=2000000)
        log += record("render-submit", a=33, b=2033000)
        summary = summarize(io.StringIO(log))
        self.assertEqual(summary["submission_interval_us"]["count"], 1)
        self.assertEqual(summary["submission_interval_us"]["mean"], 33000)

    def test_rejects_invalid_records_and_empty_input(self):
        for log in ("", "unrelated\n", "[video-debug] broken\n",
                    record("worker-convert-time", b=-1),
                    record("render-submit", b=1000) + record("render-submit", a=33, b=999)):
            with self.subTest(log=log):
                with self.assertRaises(ValueError):
                    summarize(io.StringIO(log))


if __name__ == "__main__":
    unittest.main()
