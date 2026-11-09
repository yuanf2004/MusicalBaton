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
        self.assertEqual(config.bpm, 100)
        self.assertEqual(config.serial_baud, 115200)
        self.assertEqual(config.sensitivity, "balanced")
        self.assertEqual(config.strength_threshold, 50)
        self.assertEqual(config.beat.frequency_hz, 800)

    def test_config_has_one_unmetered_cue(self):
        config = MODULE.load_config(ROOT / "tools" / "pc_audio_cues.json")
        self.assertGreater(config.beat.duration_ms, 0)

    def test_only_plays_strength_above_threshold(self):
        config = MODULE.load_config(ROOT / "tools" / "pc_audio_cues.json")
        self.assertFalse(MODULE.should_play_beat(config, 49))
        self.assertFalse(MODULE.should_play_beat(config, 50))
        self.assertTrue(MODULE.should_play_beat(config, 51))

    def test_rejects_invalid_strength_threshold(self):
        raw = (ROOT / "tools" / "pc_audio_cues.json").read_text(encoding="utf-8")
        import json
        import tempfile

        config = json.loads(raw)
        config["strength_threshold"] = 100
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "strength_threshold"):
                MODULE.load_config(path)

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
                "predicted": False,
            },
        )

    def test_parse_predicted_conducting_beat(self):
        event = MODULE.parse_console_beat(
            "BEAT: t=2500 detected=2465 strength=71 BPM=100.0 predicted=1"
        )
        self.assertTrue(event["predicted"])

    def test_ignores_non_beat_console_line(self):
        self.assertIsNone(MODULE.parse_console_beat("Beat detector calibrated"))

    def test_normalizes_old_diagnostic_record(self):
        fields = "CD,1,2,3,4,5,6,7,8,9,10,11".split(",")
        self.assertEqual(
            MODULE.normalize_diagnostic_fields(fields),
            ["1", "2", "3", "4", "", "", "", "5", "6", "7", "8", "9", "10", "11", "", "", "", ""],
        )

    def test_accepts_current_diagnostic_record(self):
        fields = ["CD"] + [str(value) for value in range(18)]
        self.assertEqual(MODULE.normalize_diagnostic_fields(fields), fields[1:])

    def test_audio_worker_drops_stale_pending_cue(self):
        worker = MODULE.AudioWorker()
        first = MODULE.Cue(800, 25)
        latest = MODULE.Cue(1200, 25)
        worker.play(first)
        worker.play(latest)
        self.assertEqual(worker._items.get_nowait(), latest)

    def test_callback_audio_replaces_active_cue(self):
        worker = MODULE.AudioWorker()
        worker._stream = object()
        worker.play(MODULE.Cue(800, 25))
        output = bytearray(256)
        worker._audio_callback(output, 128, None, None)
        self.assertTrue(any(output))
        self.assertEqual(worker._active_offset, 256)
        worker.play(MODULE.Cue(1200, 25))
        self.assertEqual(worker._active_offset, 0)


if __name__ == "__main__":
    unittest.main()
