"""Reject unverifiable captures and retain acquisition states in steady windows."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from plot_r4_trials import analyze, NAMES, SERVO


class R4AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = {'status': 'complete', 'firmware_sha256': {}, 'trials': []}
        for key in NAMES:
            text = ('tinyptp[0.500]: boot role FOLLOWER\n'
                    'tinyptp[0.500]: platform UNO_R4_MINIMA cpu_hz 48000000\n'
                    f'tinyptp[1.000]: servo {SERVO[key]}\n')
            for i in range(151):
                # A deliberately non-s3 final sample must stay in the result.
                state, offset = (2, 1000) if i == 150 else (3, 0)
                text += (f'tinyptp[{i+12}.000]: master offset {offset} s{state} '
                         f'freq +10000 path delay 735 sample {i+1} seq {i+1}\n')
                if i % 10 == 0:
                    text += (f'tinyptp[{i+12}.001]: CPU servo_last_us 6 '
                             'servo_max_us 8 usb_poll_max_us 3000\n')
            (self.root / f'{key}.log').write_text(text)
            self.manifest['firmware_sha256'][key] = 'synthetic-test-only'
            self.manifest['trials'].append({'label': key, 'example': key,
                                           'samples': 151, 'log_sha256': hashlib.sha256(text.encode()).hexdigest()})
        self.save()

    def save(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_keep_every_post_cutoff_state(self):
        results, _ = analyze(self.root, 120)
        self.assertEqual(results['pi']['window_samples'], 31)
        self.assertEqual(results['pi']['window_state_counts'], {'2': 1, '3': 30})
        self.assertEqual(results['pi']['offset_max_ns'], 1000)
        self.assertEqual(results['pi']['steady_servo_last_median_us'], 6)
        self.assertEqual(results['pi']['steady_servo_peak_us'], 8)

    def test_tampered_log_is_rejected(self):
        with (self.root / 'pi.log').open('a') as f:
            f.write('changed\n')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            analyze(self.root, 120)

    def test_failed_campaign_is_rejected(self):
        self.manifest['status'] = 'failed'
        self.save()
        with self.assertRaisesRegex(ValueError, 'must be complete'):
            analyze(self.root, 120)


if __name__ == '__main__':
    unittest.main()
