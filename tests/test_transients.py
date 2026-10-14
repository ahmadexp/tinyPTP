"""Check finite-record settling and sign-sensitive transient measurements."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from plot_transients import metrics, settling_time


def trace(offsets):
    return [dict(sample=i+1, elapsed_s=float(i), uptime_s=float(i+12),
                 offset_ns=x, frequency_ppb=0, servo_state=2)
            for i, x in enumerate(offsets)]


class TransientTests(unittest.TestCase):
    def test_late_excursion_invalidates_early_entry(self):
        rows = trace([2000] + [0]*35 + [1001] + [1000]*31)
        self.assertEqual(settling_time(rows, 1000), 37)
        self.assertIsNone(settling_time(rows[:-1], 1000))

    def test_end_outside_band_or_short_tail_is_not_settled(self):
        self.assertIsNone(settling_time(trace([0]*60 + [101]), 100))
        self.assertIsNone(settling_time(trace([0]*30), 100))
        self.assertEqual(settling_time(trace([100]*31), 100), 0)

    def test_missing_sample_and_time_gap_break_observation(self):
        rows = trace([0]*61)
        for row in rows[31:]:
            row["sample"] += 1
        self.assertIsNone(settling_time(rows, 100))
        rows = trace([0]*61)
        for row in rows[30:]:
            row["elapsed_s"] += 2
        self.assertEqual(settling_time(rows, 100), 32)

    def test_negative_initial_error_and_window_boundary(self):
        rows = trace([-10000, -5000, 50] + [0]*117 + [1000])
        rows[1]["frequency_ppb"] = 500000
        rows[2]["servo_state"] = 3
        result = metrics(rows)
        self.assertEqual(result["opposite_peak_0_120s_ns"], 50)
        self.assertEqual(result["opposite_peak_time_s"], 2)
        self.assertEqual(result["saturated_samples_0_120s"], 1)
        self.assertEqual(result["s3_exits"], 1)
        self.assertEqual(result["first_s3_s"], 2)

    def test_zero_initial_offset_has_no_opposite_direction(self):
        self.assertIsNone(metrics(trace([0, -2, 1]))["opposite_peak_0_120s_ns"])


if __name__ == "__main__":
    unittest.main()
