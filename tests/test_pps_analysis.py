import importlib.util
from pathlib import Path
from decimal import Decimal as D
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('analyze_pps',Path(__file__).resolve().parents[1]/'tools/analyze_pps.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class PpsAnalysisTests(unittest.TestCase):
    def test_decimal_precision_and_sign(self):
        a=[D('800.123456789'),D('801.123456789')]
        b=[D('800.123456787'),D('801.123456795')]
        pairs,ua,ub=m.pair_edges(a,b)
        self.assertEqual([p[2] for p in pairs],[D(-2),D(6)])
        self.assertEqual((ua,ub),([],[]))

    def test_missing_edge_does_not_shift_pairing(self):
        pairs,ua,ub=m.pair_edges(list(map(D,['1','2','3'])),list(map(D,['1.000000010','3.000000020'])))
        self.assertEqual(ua,[D(2)]);self.assertEqual(ub,[])
        self.assertEqual([p[2] for p in pairs],[D(10),D(20)])

    def test_glitch_is_rejected(self):
        with self.assertRaises(ValueError):m.pair_edges([D(1),D('1.001')],[D(1)])

    def test_initial_high_is_not_a_rising_edge(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'digital.csv'
            p.write_text('Time [s],Channel 0,Channel 1\n0,1,1\n0.5,0,0\n1,1,0\n1.000000010,1,1\n1.5,0,0\n2,1,1\n2.5,0,0\n')
            edges=m.read_edges(p)
            self.assertEqual(edges[0]['rising'],[D(1),D(2)])
            summary,_=m.analyze(p,100000000)
            self.assertEqual(summary['edges']['rising']['mean_ns'],5)
            self.assertEqual(summary['channels']['0']['high_pulse_width']['mean_ns'],500000000)
            self.assertEqual(summary['sample_interval_ns'],10)

    def test_flat_capture_not_a_measurement(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'digital.csv';p.write_text('Time [s],Channel 0,Channel 1\n0,0,0\n1,0,0\n')
            with self.assertRaises(ValueError):m.analyze(p,100000000)

    def test_qualification_excludes_whole_glitched_pulse_and_boundary(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'digital.csv'
            p.write_text('Time [s],Channel 0,Channel 1\n0,1,1\n0.4,0,0\n0.9,1,1\n1.4,0,0\n1.9,1,1\n2.0,0,0\n2.000000002,1,1\n2.4,0,0\n2.9,1,1\n3.4,0,0\n3.9,1,1\n4.0,1,1\n')
            with self.assertRaises(ValueError):m.analyze(p,500000000)
            summary,pairs=m.analyze(p,500000000,qualify_500ms=True)
            self.assertEqual([a for a,b,d in pairs['rising']],[D('.9'),D('2.9')])
            q=summary['pulse_qualification']['channels']['0']
            self.assertEqual(q['accepted_complete_pulses'],2)
            self.assertEqual(len(q['rejected_complete_pulses']),2)
            self.assertEqual(q['excluded_falling_s'],['0.4','2.0','2.4'])

    def test_notch_near_rising_edge_does_not_move_the_measured_edge(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'digital.csv'
            p.write_text('Time [s],Channel 0,Channel 1\n0,1,1\n0.4,0,0\n0.9,1,1\n1.4,0,0\n1.9,1,1\n1.900000016,1,0\n1.900000018,1,1\n2.4,0,0\n2.9,1,1\n3.4,0,0\n')
            summary,pairs=m.analyze(p,500000000,qualify_500ms=True)
            self.assertEqual([a for a,b,d in pairs['rising']],[D('.9'),D('2.9')])
            self.assertEqual(summary['edges']['rising']['unmatched_ch0_seconds'],['1.9'])
            self.assertEqual(len(summary['pulse_qualification']['channels']['1']['rejected_complete_pulses']),2)

if __name__=='__main__':unittest.main()
