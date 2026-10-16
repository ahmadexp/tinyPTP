import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from plot_saleae_trials import physical_settling


class PhysicalSettlingTests(unittest.TestCase):
    def test_contiguous_observed_suffix(self):
        pairs=[(float(t),200 if t<10 else 20) for t in range(61)]
        self.assertEqual(physical_settling(pairs,100,60.5),10)

    def test_gap_prevents_claim_across_missing_pulse(self):
        pairs=[(float(t),20) for t in range(61) if t!=40]
        self.assertIsNone(physical_settling(pairs,100,60.5))
        pairs=[(float(t),20) for t in range(61) if t!=20]
        self.assertEqual(physical_settling(pairs,100,60.5),21)

    def test_unobserved_end_is_not_a_settling_result(self):
        pairs=[(float(t),20) for t in range(61)]
        self.assertIsNone(physical_settling(pairs,100,64))

    def test_short_or_out_of_band_record(self):
        self.assertIsNone(physical_settling([],100,60))
        self.assertIsNone(physical_settling([(0,0),(1,0)],100,1.5))
        self.assertIsNone(physical_settling([(float(t),200) for t in range(61)],100,60.5))


if __name__=='__main__':unittest.main()
