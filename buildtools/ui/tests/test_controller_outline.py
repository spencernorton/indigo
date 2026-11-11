#!/usr/bin/env python3
"""Keep the controller shell reproducible, bounded and clear of its controls."""
import math
from pathlib import Path
import re
import subprocess
import sys
import unittest

from test_cheats_gx_stream import extract_function

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "cube/swiss/source/gui/indigo_background.c"


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def intersections(points):
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        for j in range(i + 2, len(points)):
            if j == (i - 1) % len(points):
                continue
            c, d = points[j], points[(j + 1) % len(points)]
            if cross(a, b, c) * cross(a, b, d) < 0 and cross(c, d, a) * cross(c, d, b) < 0:
                yield i, j


def offset_band(points, half_width, side):
    """Offset the source polyline as drawFaceBand does before projection."""
    result = []
    for i, point in enumerate(points):
        before, after = points[i - 1], points[(i + 1) % len(points)]
        ax, ay = point[0] - before[0], point[1] - before[1]
        bx, by = after[0] - point[0], after[1] - point[1]
        al, bl = math.hypot(ax, ay), math.hypot(bx, by)
        nx, ny = -ay / al - by / bl, ax / al + bx / bl
        length = math.hypot(nx, ny)
        nx, ny = nx / length, ny / length
        reach = half_width / max(nx * (-ay / al) + ny * (ax / al), 0.5)
        result.append((point[0] + nx * reach * side, point[1] + ny * reach * side))
    return result


def inside(point, polygon):
    x, y = point
    result = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]:
            result = not result
    return result


def distance(point, polygon):
    best = math.inf
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        t = max(0, min(1, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        best = min(best, math.hypot(point[0] - a[0] - t * dx, point[1] - a[1] - t * dy))
    return best


class ControllerOutlineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()
        cls.table = re.search(r"static const float controllerOutline\[\]\[2\] = \{\n(.*?)\n\};",
                              cls.source, re.S).group(1)
        cls.points = [tuple(map(float, point)) for point in
                      re.findall(r"\{(-?[\d.]+)f, (-?[\d.]+)f\}", cls.table)]
        cls.draw = extract_function(cls.source, "static void drawControllerIcon(")
        cls.width = float(re.search(r"drawFaceBand\(raster, face, outline, outlineCount, ([\d.]+)f,",
                                   cls.draw).group(1))

    def test_generated_table_is_current(self):
        generated = subprocess.check_output([sys.executable, str(ROOT / "buildtools/ui/controller_outline.py")],
                                            text=True).rstrip()
        self.assertEqual(generated, self.table)

    def test_shell_and_solid_band_do_not_cross(self):
        # The former outline used 65 points. Keep its replacement within the
        # same cost and the native emitter's capacity, with a clockwise loop.
        limit = int(re.search(r"#define FACE_BAND_MAX (\d+)", self.source).group(1))
        self.assertLessEqual(len(self.points), min(65, limit))
        area = sum(a[0] * b[1] - b[0] * a[1] for a, b in
                   zip(self.points, self.points[1:] + self.points[:1]))
        self.assertLess(area, 0)
        self.assertFalse(list(intersections(self.points)))
        for side in (-1, 1):
            with self.subTest(side=side):
                self.assertFalse(list(intersections(offset_band(self.points, self.width, side))))

    def test_lower_controls_clear_the_shell(self):
        points = []
        for values in re.findall(r"drawControllerRect\(raster, face, ((?:-?[\d.]+f, ){3}-?[\d.]+f), plane,",
                                 self.draw):
            x0, y0, x1, y1 = [float(value.rstrip("f")) for value in values.split(", ")]
            points.extend(((x0, y0), (x0, y1), (x1, y0), (x1, y1)))
        self.assertEqual(len(points), 16)
        # The second ring is the C-stick's octagonal gate. Check its entire
        # circumference, including the cap's largest travel plus its radius.
        rings = re.findall(r"drawFaceRing\(raster, face, (-?[\d.]+)f, (-?[\d.]+)f, ([\d.]+)f, 8, ([\d.]+)f,",
                           self.draw)
        self.assertEqual(len(rings), 2)
        cx, cy, radius, width = map(float, rings[1])
        for step in range(32):
            angle = math.tau * step / 32
            points.append((cx + (radius + width) * math.cos(angle),
                           cy + (radius + width) * math.sin(angle)))
        for point in points:
            with self.subTest(point=point):
                self.assertTrue(inside(point, self.points))
                # Shell half-width plus a native-pixel fringe and a visible
                # gap: the additive passes must not brighten each other.
                self.assertGreater(distance(point, self.points), self.width + 0.012)


if __name__ == "__main__":
    unittest.main()
