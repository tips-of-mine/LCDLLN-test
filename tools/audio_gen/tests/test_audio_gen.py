"""Tests du générateur audio (tools/audio_gen) — exécutés par la CI audio-gen."""

from __future__ import annotations

import shutil
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lcdlln_audio import dsp, percussion as perc, textures  # noqa: E402
from lcdlln_audio.orchestra import GM, Part, note, render, write_midi  # noqa: E402
import menu_theme  # noqa: E402


class DspTests(unittest.TestCase):
    def test_wrap_loop_continuity(self):
        # Un signal continu de longueur L + queue, replié : la jonction fin → début
        # doit être aussi douce que l'intérieur du signal.
        n = dsp.samples(2.0)
        t = np.arange(n + dsp.samples(0.5)) / dsp.SR
        x = np.sin(2 * np.pi * 5.0 * t)  # 5 Hz : 10 périodes exactes en 2 s
        y = dsp.wrap_loop(x, n)
        self.assertEqual(len(y), n)
        seam = abs(y[0, 0] - y[-1, 0])
        inner = np.max(np.abs(np.diff(y[:, 0])))
        self.assertLess(seam, inner * 4 + 1e-3)

    def test_limiter_ceiling(self):
        x = dsp.rng(1).standard_normal((48000, 2)) * 3.0
        y = dsp.limiter(x, -1.0)
        self.assertLessEqual(float(np.max(np.abs(y))), dsp.from_db(-1.0) + 1e-6)

    def test_filters_keep_float32(self):
        x = np.ones(1000, dtype=np.float32)
        self.assertEqual(dsp.lowpass(x, 1000.0).dtype, np.float32)

    def test_lufs_normalization(self):
        x = dsp.rng(2).standard_normal((dsp.samples(5.0), 2)) * 0.1
        y = dsp.normalize_lufs(x, -18.0)
        self.assertAlmostEqual(dsp.loudness_lufs(y), -18.0, delta=0.2)


class SynthTests(unittest.TestCase):
    def test_percussion_normalized_and_finite(self):
        for fn in (perc.taiko, perc.battle_drum, perc.frame_drum, perc.finger_drum, perc.shaker,
                   perc.woodblock, perc.anvil, perc.chains, perc.cinematic_boom):
            s = fn(3)
            self.assertTrue(np.all(np.isfinite(s)), fn.__name__)
            self.assertAlmostEqual(float(np.max(np.abs(s))), 1.0, delta=0.05, msg=fn.__name__)
            self.assertLess(abs(float(s[0])), 0.05, fn.__name__)  # pas de clic d'attaque

    def test_textures(self):
        w = textures.wind(2.0, 1)
        self.assertEqual(w.shape, (dsp.samples(2.0), 2))
        self.assertTrue(np.all(np.isfinite(textures.thunder(2, 3.0))))


class ScoreTests(unittest.TestCase):
    def test_note_names(self):
        self.assertEqual(note("D4"), 62)
        self.assertEqual(note("Bb3"), 58)
        self.assertEqual(note("C#5"), 73)

    def test_menu_theme_form(self):
        h = menu_theme.build_harmony()
        self.assertEqual(len(h), menu_theme.TOTAL_BARS)
        self.assertEqual(sum(c for _, _, c in menu_theme.SECTIONS), menu_theme.TOTAL_BARS)
        self.assertTrue(300 <= menu_theme.LOOP_SECONDS <= 600)
        # Boucle : la fin reproduit l'ouverture (pédale de ré).
        self.assertEqual(h[-8:], h[:8])
        parts = menu_theme.compose(h)
        self.assertGreater(len(parts["horn_solo"].notes), 20)

    def test_midi_written(self):
        p = Part("t", GM["horn"])
        p.add(0, 1, "D4", 90)
        out = Path(__file__).with_name("_tmp.mid")
        try:
            write_midi(p, 72, out)
            self.assertGreater(out.stat().st_size, 20)
        finally:
            out.unlink(missing_ok=True)

    @unittest.skipUnless(shutil.which("fluidsynth") and Path("/usr/share/sounds/sf2/FluidR3_GM.sf2").exists(),
                         "FluidSynth / FluidR3_GM absents")
    def test_render_short(self):
        p = Part("t", GM["strings"])
        p.add(0, 2, "D3", 90)
        x = render(p, 72, 2.0)
        self.assertEqual(x.shape, (dsp.samples(2.0), 2))
        self.assertGreater(float(np.max(np.abs(x))), 0.01)


if __name__ == "__main__":
    unittest.main()
