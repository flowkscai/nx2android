"""The Android build compiles nxnative/dm/gfx/nvn/nvn_thunks.S. The default table must not count."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
THUNKS = ROOT / 'nxnative/dm/gfx/nvn/nvn_thunks.S'
HLE = ROOT / 'nxnative/dm/gfx/nvn/hle_nvn.c'
BUILD = ROOT / 'nxnative/dm/android/build.py'


def bodies(text):
    """Map each thunk label to the instructions up to the next global symbol."""
    found = {}
    for match in re.finditer(r'\n(_nvnthc?_\d+):\n(.*?)(?=\n    \.globl |\n    \.data\n)', text, re.S):
        found[match.group(1)] = match.group(2)
    return found


class NvnThunks(unittest.TestCase):
    def test_android_build_compiles_the_shipped_table(self):
        build = BUILD.read_text(encoding='utf-8')
        self.assertIn('gfx/nvn/nvn_thunks.S', build)

    def test_default_thunks_do_not_stadd(self):
        text = THUNKS.read_text(encoding='utf-8')
        self.assertIn('_nvn_thunk:', text)
        self.assertIn('_nvn_thunk_count:', text)
        default = text.split('_nvn_thunk:\n', 1)[1].split('_nvn_thunk_count:', 1)[0]
        counted = text.split('_nvn_thunk_count:\n', 1)[1].split('_nvn_names:', 1)[0]
        default_labels = re.findall(r'\.quad (_nvnth_\d+)\b', default)
        count_labels = re.findall(r'\.quad (_nvnthc_\d+)\b', counted)
        self.assertGreater(len(default_labels), 500)
        self.assertEqual(len(default_labels), len(count_labels))
        code = bodies(text)
        for label in default_labels:
            body = code[label]
            self.assertNotIn('stadd', body, label)
            self.assertIn('ldr x17, [x16, #8]', body)
            self.assertIn('br x17', body)
        for label in count_labels:
            body = code[label]
            self.assertIn('stadd', body, label)
            self.assertIn('ldr x17, [x16, #8]', body)

    def test_counting_table_is_selected_only_when_counts_are_on(self):
        source = HLE.read_text(encoding='utf-8')
        lookup = source.split('static void *nvn_lookup(', 1)[1].split('HLE_FORCE', 1)[0]
        self.assertIn('nvn_counting() ? nvn_thunk_count : nvn_thunk', lookup)
        counting = source.split('static int nvn_counting(void)', 1)[1].split('static void *nvn_lookup', 1)[0]
        self.assertIn('DM_NVN_COUNT', counting)
        self.assertIn('DM_NVN_TSV', counting)


if __name__ == '__main__':
    unittest.main()
