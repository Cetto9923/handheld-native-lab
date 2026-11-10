import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2] / 'scripts/graphics/analyze_frame_metrics.py'
spec = importlib.util.spec_from_file_location('frame_metrics', path)
metrics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metrics)


def row(frame, ns, work):
    return dict(frame=frame, monotonic_ns=ns, texture_serial=1, image_writer=work,
                draws=work, dispatches=0, submissions=work, present_result=0)


class FrameMetrics(unittest.TestCase):
    def test_skipped_failed_work_cannot_inflate_present_fps(self):
        rows = [row(i + 1, i * 10_000_000, i + 1) for i in range(5)]
        rows[1]['present_result'] = 1
        rows[2]['present_result'] = -1
        result = metrics.analyze(rows, 'drops')
        self.assertEqual(result['attempts'], 5)
        self.assertEqual(result['queued_presents'], 3)
        self.assertEqual(result['skipped_attempts'], 1)
        self.assertEqual(result['failed_attempts'], 1)
        self.assertEqual(result['present_fps'], 50)
        self.assertEqual(result['attempt_fps'], 100)
        self.assertEqual(result['worst_intervals'][0]['interval_ms'], 30)
        self.assertEqual(result['distinct_render_work_frames'], 3)
        self.assertEqual(result['rows_removed_for_frame_time'], 0)

    def test_legacy_trace_has_no_verified_present_rate(self):
        rows = [row(1, 0, 1), row(2, 10_000_000, 2)]
        for r in rows:
            del r['present_result']
        result = metrics.analyze(rows, 'legacy')
        self.assertIsNone(result['present_fps'])
        self.assertIsNone(result['presents'])
        self.assertEqual(result['attempt_fps'], 100)
        self.assertEqual(result['outcome_evidence'], 'UNRECORDED_LEGACY')

    def test_all_skipped_and_invalid_or_mixed_outcomes(self):
        rows = [row(1, 0, 1), row(2, 10_000_000, 2)]
        for r in rows:
            r['present_result'] = 1
        result = metrics.analyze(rows, 'background')
        self.assertEqual(result['present_fps'], 0)
        self.assertEqual(result['presents'], 0)
        self.assertEqual(result['worst_intervals'], [])
        rows[0]['present_result'] = 99
        with self.assertRaises(ValueError):
            metrics.analyze(rows, 'invalid')
        rows[0]['present_result'] = 0
        del rows[1]['present_result']
        with self.assertRaises(ValueError):
            metrics.analyze(rows, 'mixed')

    def test_sixty_frames_with_no_drop(self):
        rows = [row(i + 1, round(i * 1e9 / 60), i + 1) for i in range(121)]
        result = metrics.analyze(rows, 'fixed fixture')
        self.assertAlmostEqual(result['distinct_render_work_fps'], 60)
        self.assertLess(result['interval_ms']['p99'], 16.667)
        self.assertEqual(result['repeated_presents'], 0)

    def test_repeats_and_long_frames_remain_visible(self):
        rows = [row(1, 0, 1), row(2, 10_000_000, 1), row(3, 210_000_000, 2)]
        result = metrics.analyze(rows, 'mixed')
        self.assertEqual(result['repeated_presents'], 1)
        self.assertEqual(result['over_100ms'], 1)
        self.assertEqual(result['interval_ms']['p95'], 200)
        self.assertEqual(result['rows_removed_for_frame_time'], 0)
        self.assertLess(result['distinct_render_work_fps'], result['present_fps'])

    def test_rare_stall_has_timestamp_even_when_p99_looks_good(self):
        rows = [row(i + 1, i * 10_000_000 + (190_000_000 if i >= 500 else 0), i + 1)
                for i in range(1001)]
        result = metrics.analyze(rows, 'one rare stall')
        self.assertEqual(result['interval_ms']['p99'], 10)
        self.assertEqual(result['intervals_over_ms']['50'], 1)
        worst = result['worst_intervals'][0]
        self.assertEqual((worst['frame_before'], worst['frame_after']), (500, 501))
        self.assertEqual(worst['interval_ms'], 200)
        self.assertEqual(worst['end_monotonic_ns'], 5_190_000_000)
        self.assertEqual(result['rows_removed_for_frame_time'], 0)

    def test_threshold_is_strict_and_empty_trace_has_no_worst_frame(self):
        result = metrics.analyze([row(1, 0, 1), row(2, 50_000_000, 2)], 'boundary')
        self.assertEqual(result['intervals_over_ms']['40'], 1)
        self.assertEqual(result['intervals_over_ms']['50'], 0)
        self.assertEqual(metrics.analyze([], 'empty')['worst_intervals'], [])

    def test_bad_input_is_not_silently_dropped(self):
        with self.assertRaises(ValueError):
            metrics.analyze([{'frame': '1'}], 'bad')
        with self.assertRaises(ValueError):
            metrics.analyze([row(2, 10, 1), row(1, 20, 2)], 'bad')

    def test_alternating_stale_images_are_not_new_render_work(self):
        a = row(1, 0, 1)
        b = row(2, 10_000_000, 2)
        b['texture_serial'] = 2
        c = row(3, 20_000_000, 1)
        c['draws'] = 99  # An unrelated draw cannot refresh image A.
        result = metrics.analyze([a, b, c], 'alternating')
        self.assertEqual(result['distinct_render_work_frames'], 2)
        self.assertEqual(result['repeated_presents'], 1)


if __name__ == '__main__':
    unittest.main()
