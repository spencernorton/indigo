#!/usr/bin/env python3
"""Regenerate the Library emblem's clockwise GameCube controller silhouette.

The shell is traced from Nintendo's front-facing original-controller product
photograph, leaving out the cable and triggers (drawn separately):
https://www.nintendo.co.jp/ngc/specific/grap/gamecube.jpg

The photograph's indigo body pixels were segmented and their boundary traced,
then scaled so the stick and A button land on the emblem's u = -/+0.335,
v = 0.075 with Start on u = 0. The left half is kept and mirrored: the right
shoulder carries the Z button, and the top centre, where the cable leaves,
follows the shoulder arch. Coordinates are in face units. Only the simplified
table is used on the console; no image or curve sampling is kept in the draw
path.
"""
import math

# Traced left half, from the top centre round the left grip and D-pad lobe to
# the top of the opening between the lower lobes.
LEFT_HALF = (
    (0.000, 0.372), (-0.045, 0.371), (-0.141, 0.359), (-0.183, 0.352),
    (-0.197, 0.345), (-0.253, 0.331), (-0.279, 0.321), (-0.336, 0.308),
    (-0.377, 0.287), (-0.394, 0.283), (-0.406, 0.273), (-0.440, 0.256),
    (-0.504, 0.190), (-0.532, 0.134), (-0.540, 0.091), (-0.540, 0.064),
    (-0.547, 0.038), (-0.547, -0.063), (-0.550, -0.094), (-0.550, -0.195),
    (-0.540, -0.281), (-0.532, -0.314), (-0.520, -0.342), (-0.495, -0.371),
    (-0.469, -0.380), (-0.451, -0.380), (-0.427, -0.370), (-0.402, -0.338),
    (-0.376, -0.253), (-0.376, -0.232), (-0.365, -0.173), (-0.352, -0.151),
    (-0.342, -0.154), (-0.323, -0.179), (-0.313, -0.199), (-0.307, -0.223),
    (-0.291, -0.250), (-0.271, -0.271), (-0.244, -0.289), (-0.211, -0.301),
    (-0.158, -0.301), (-0.136, -0.296), (-0.095, -0.273), (-0.073, -0.249),
    (-0.062, -0.233), (-0.054, -0.213), (-0.045, -0.179), (-0.045, -0.141),
    (-0.059, -0.086), (-0.072, -0.063), (-0.086, -0.047), (-0.122, -0.015),
    (-0.126, -0.002), (-0.123, 0.002), (-0.105, 0.006), (-0.078, 0.010),
    (0.000, 0.013),
)


def douglas_peucker(points, eps):
    a, b = points[0], points[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy) or 1e-9
    best, index = 0.0, 0
    for i in range(1, len(points) - 1):
        d = abs(dy * (points[i][0] - a[0]) - dx * (points[i][1] - a[1])) / length
        if d > best:
            best, index = d, i
    if best > eps:
        return douglas_peucker(points[:index + 1], eps)[:-1] + douglas_peucker(points[index:], eps)
    return [a, b]


def closed_simplify(points, eps):
    far = max(range(len(points)), key=lambda i: math.dist(points[i], points[0]))
    return (douglas_peucker(points[:far + 1], eps)[:-1] +
            douglas_peucker(points[far:] + [points[0]], eps)[:-1])


def corner_cut(points):
    # Round the four concave lobe junctions before offsetting the outline
    # into a band, so its inner boundary never folds back over a sharp cusp.
    return [q for i, p in enumerate(points) for q in (
        tuple(0.75 * p[k] + 0.25 * points[(i + 1) % len(points)][k] for k in range(2)),
        tuple(0.25 * p[k] + 0.75 * points[(i + 1) % len(points)][k] for k in range(2)))]


def outline():
    points = list(LEFT_HALF) + [(-u, v) for u, v in reversed(LEFT_HALF[1:-1])]
    loop = closed_simplify(points, 0.015)
    for _ in range(2):
        loop = corner_cut(loop)
    loop = closed_simplify(loop, 0.005)
    area = sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(loop, loop[1:] + loop[:1]))
    return loop if area < 0 else loop[::-1]


def literal(value):
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    text = "0" if text in ("-0", "") else text
    return (text if "." in text else text + ".0") + "f"


if __name__ == "__main__":
    points = [(round(u, 3), round(v, 3)) for u, v in outline()]
    rows = ["\t\t" + ", ".join("{%s, %s}" % (literal(u), literal(v)) for u, v in points[k:k + 5])
            for k in range(0, len(points), 5)]
    print(",\n".join(rows))
