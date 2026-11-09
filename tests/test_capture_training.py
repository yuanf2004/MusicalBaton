import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "capture_training", ROOT / "tools" / "capture_training.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CaptureTrainingTest(unittest.TestCase):
    def test_parse_packet(self):
        packet = b"MBR1" + struct.pack("<HIhhhhhh", 7, 1234, -1, 2, 1000, -4, 5, -6)
        self.assertEqual(MODULE.parse_packet(packet), (7, 1234, -1, 2, 1000, -4, 5, -6))

    def test_rejects_bad_marker(self):
        with self.assertRaises(ValueError):
            MODULE.parse_packet(bytes(MODULE.PACKET_SIZE))


if __name__ == "__main__":
    unittest.main()
