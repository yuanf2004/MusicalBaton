import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pc_audio_cues", ROOT / "tools" / "pc_audio_cues.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class PcAudioCuesTest(unittest.TestCase):
    def test_parse_beat(self):
        packet = bytearray(struct.pack("<BBHIHBB", 1, 1, 65535, 1234, 987, 72, 0))
        self.assertEqual(MODULE.parse_beat(packet), (65535, 1234, 987, 72))

    def test_rejects_invalid_packet(self):
        with self.assertRaises(ValueError):
            MODULE.parse_beat(bytearray(11))

    def test_default_config(self):
        config = MODULE.load_config(ROOT / "tools" / "pc_audio_cues.json")
        self.assertEqual(config.beats_per_measure, 4)
        self.assertEqual(config.bpm, 100)
        self.assertFalse(config.emphasize_beat_1)
        self.assertEqual(config.serial_baud, 115200)
        self.assertEqual(config.sensitivity, "balanced")
        self.assertGreater(config.beat_1.frequency_hz, config.other_beat.frequency_hz)

    def test_default_has_no_downbeat_emphasis(self):
        config = MODULE.load_config(ROOT / "tools" / "pc_audio_cues.json")
        self.assertEqual(MODULE.cue_for_beat(config, 1), config.other_beat)
        self.assertEqual(MODULE.cue_for_beat(config, 4), config.other_beat)
        self.assertEqual(config.other_beat.duration_ms, 25)

    def test_parse_conducting_console_beat(self):
        event = MODULE.parse_console_beat(
            "BEAT: t=2400 detected=2460 strength=71 BPM=100.0"
        )
        self.assertEqual(
            event,
            {
                "timestamp_ms": 2400,
                "detected_ms": 2460,
                "strength": 71,
                "bpm": 100.0,
            },
        )

    def test_ignores_non_beat_console_line(self):
        self.assertIsNone(MODULE.parse_console_beat("Beat detector calibrated"))


if __name__ == "__main__":
    unittest.main()
