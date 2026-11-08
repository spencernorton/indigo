"""Check the timing budget and that the vendor extension leaves stock code intact."""
import hashlib
import json
from pathlib import Path
import re
import unittest

HERE = Path(__file__).resolve().parent


class TimingTests(unittest.TestCase):
    def test_original_driver_is_preserved(self):
        source = (HERE / 'sdk/video_540_driver.c').read_text()
        source = source[source.index('/*-------------------------------------------------------------'):]
        source = re.sub(r'/\* HD60 enhanced non-interlace timing.*?(?=static const struct _timing\* __gettiming)',
                        '', source, flags=re.S)
        source = source.replace('\t\tcase ((6u << 2) | VI_PROGRESSIVE):\n\t\t\treturn &indigo540Timing;\n', '')
        provenance = json.loads((HERE / 'sdk/provenance.json').read_text())
        self.assertEqual(hashlib.sha256(source.encode()).hexdigest(), provenance['source_sha256'])

    def test_raster_has_blanking_and_near_sixty_hz(self):
        source = (HERE / 'sdk/video_540_driver.c').read_text()
        initializer = source.split('indigo540Timing = {', 1)[1].split('};', 1)[0]
        fields = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', initializer)]
        equ, acv, prb_odd, prb_even, psb_odd, psb_even = fields[:6]
        nhlines, hlw = fields[14:16]
        self.assertEqual(acv, 540)
        self.assertEqual((prb_odd, psb_odd), (prb_even, psb_even))
        self.assertEqual(nhlines, equ * 3 + acv * 2 + prb_odd + psb_odd)
        self.assertEqual((2 * hlw, nhlines // 2), (800, 563))
        self.assertGreater(2 * hlw, 720)
        self.assertGreater(nhlines // 2, acv)
        self.assertAlmostEqual(27_000_000 / (hlw * nhlines), 59.946714, places=5)
        # libogc2 adds an origin of -40/+40 when programming HBE/HBS.
        self.assertEqual((fields[19] - 40, fields[20] + 40), (75, 395))


if __name__ == '__main__':
    unittest.main()
