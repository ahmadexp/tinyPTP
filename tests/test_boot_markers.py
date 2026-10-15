"""Boot timestamps can be nonzero with native USB CDC startup waiting."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from plot_follower import boot_count, read_samples

class BootMarkerTests(unittest.TestCase):
    def capture(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'serial.log'
            p.write_text(text)
            return read_samples(p)

    def test_r4_nonzero_boot_discards_previous_buffered_session(self):
        text = ('tinyptp[0.000]: boot role FOLLOWER\n'
                'tinyptp[100.000]: master offset 999 s3 freq +1 path delay 700 sample 90 seq 90\n'
                'tinyptp[2.153]: boot role FOLLOWER\n'
                'tinyptp[13.000]: master offset 5 s2 freq -1 path delay 730 sample 1 seq 1\n'
                'tinyptp[14.000]: master offset 4 s3 freq -2 path delay 731 sample 2 seq 2\n')
        self.assertEqual(boot_count(text), 2)
        rows = self.capture(text)
        self.assertEqual([r['offset_ns'] for r in rows], [5, 4])
        self.assertEqual([r['elapsed_s'] for r in rows], [0, 1])

    def test_missing_boot_stays_an_error(self):
        with self.assertRaisesRegex(ValueError, 'boot marker'):
            self.capture('tinyptp[1.000]: CPU servo_last_us 1\n')

    def test_r3_marker_remains_valid(self):
        self.assertEqual(boot_count('tinyptp[0.000]: boot role GRANDMASTER\n'), 1)

if __name__ == '__main__':
    unittest.main()
