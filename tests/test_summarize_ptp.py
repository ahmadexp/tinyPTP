import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'summary', Path(__file__).resolve().parents[1] / 'tools/summarize_ptp.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class LogSummaryTests(unittest.TestCase):
    def test_new_style_states_and_signed_frequency(self):
        text = '''tinyptp[1.000]: master offset 999 s0 freq +0 path delay 700 sample 1 seq 1
 tinyptp[2.000]: master offset 600000000 s1 freq +0 path delay 700 sample 2 seq 2
 tinyptp[2.001]: clock stepped; acquiring residual offset
 tinyptp[3.000]: master offset -30 s2 freq -10000 path delay 730 sample 3 seq 3
 tinyptp[4.000]: master offset 40 s3 freq +12 path delay 740 sample 4 seq 4
'''
        result = summary.summarize(text, 2)
        self.assertEqual(result['samples'], 3)
        self.assertEqual(result['tail_first_sample'], 3)
        self.assertEqual(result['offset_mean_ns'], 5)
        self.assertEqual(result['path_mean_ns'], 735)
        self.assertEqual(result['last_freq_ppb'], 12)
        self.assertEqual(result['coarse_steps'], 1)

    def test_previous_hardware_archive_is_unchanged(self):
        import json
        root = Path(__file__).resolve().parents[1] / 'docs/results/2026-10-13'
        expected = json.loads((root / 'pair-follower-04-summary.json').read_text())
        self.assertEqual(summary.summarize((root / 'logs/pair-follower-04.log').read_text(), 60), expected)


if __name__ == '__main__':
    unittest.main()
