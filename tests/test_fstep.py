"""Frame-step decisions shared by the Android host. The checks call nvn_fstep_next and nvn_fstep_gpu_slowed."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'nxnative/dm/gfx/tests/t_fstep.c'


class FrameStep(unittest.TestCase):
    def test_shipped_decisions(self):
        cc = shutil.which('cc') or shutil.which('clang')
        self.assertTrue(cc, 'a C compiler is required to run the frame-step checks')
        with tempfile.TemporaryDirectory(prefix='nx2a_fstep_') as tmp:
            exe = Path(tmp) / 't_fstep'
            build = subprocess.run(
                [cc, '-O1', '-Wall', '-Wextra', '-Werror', str(SOURCE), '-o', str(exe)],
                cwd=SOURCE.parent, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            print(run.stdout, end='')
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('== ', run.stdout)
            self.assertIn('0 failed', run.stdout)


if __name__ == '__main__':
    unittest.main()
