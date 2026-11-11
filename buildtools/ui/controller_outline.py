#!/usr/bin/env python3
"""Regenerate the Library emblem's clockwise GameCube controller silhouette.

The shell is hand-fitted to Nintendo's front-facing original-controller
product photograph, leaving out the cable and triggers (drawn separately):
https://www.nintendo.co.jp/ngc/specific/grap/gamecube.jpg

Cubic landmarks preserve the arched shoulder bridge, straight outer grips,
rounded grip tips and the deep opening between the two lower control lobes.
Coordinates are in face units. The D-pad and C-stick sit 0.025 units higher
to fit the original shell; their live input behavior is unchanged.
Only the simplified table is used on the console; no image or curve sampling
is kept in the draw path.
"""
import math

# Front-view shell landmarks, scaled around the controller icon. Each row
# is a cubic's two handles and endpoint; the previous endpoint is its start.
START = (0.0, 0.378)
CURVES = (
    ((0.133, 0.378), (0.303, 0.343), (0.403, 0.285)),
    ((0.478, 0.245), (0.520, 0.188), (0.528, 0.093)),
    ((0.533, -0.038), (0.548, -0.250), (0.508, -0.323)),
    ((0.485, -0.370), (0.433, -0.370), (0.405, -0.335)),
    ((0.370, -0.288), (0.368, -0.195), (0.338, -0.138)),
    ((0.310, -0.225), (0.258, -0.285), (0.183, -0.283)),
    ((0.108, -0.283), (0.048, -0.240), (0.043, -0.173)),
    ((0.038, -0.100), (0.070, -0.045), (0.113, 0.010)),
    ((0.053, 0.023), (-0.053, 0.023), (-0.113, 0.008)),
    ((-0.073, -0.040), (-0.035, -0.103), (-0.038, -0.170)),
    ((-0.038, -0.233), (-0.093, -0.285), (-0.168, -0.285)),
    ((-0.250, -0.285), (-0.300, -0.225), (-0.335, -0.138)),
    ((-0.363, -0.193), (-0.363, -0.280), (-0.405, -0.335)),
    ((-0.430, -0.368), (-0.483, -0.368), (-0.505, -0.318)),
    ((-0.543, -0.240), (-0.528, -0.028), (-0.523, 0.090)),
    ((-0.518, 0.188), (-0.480, 0.248), (-0.405, 0.285)),
    ((-0.303, 0.345), (-0.135, 0.378), START),
)


def curve_points(start, handles, steps=24):
    a, b, end = handles
    for i in range(1, steps + 1):
        t = i / steps
        s = 1 - t
        yield tuple(s ** 3 * start[k] + 3 * s * s * t * a[k] +
                    3 * s * t * t * b[k] + t ** 3 * end[k] for k in range(2))


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
    points = [START]
    for handles in CURVES:
        points.extend(curve_points(points[-1], handles))
    loop = closed_simplify(points[:-1], 0.006)
    loop = closed_simplify(corner_cut(corner_cut(loop)), 0.005)
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
