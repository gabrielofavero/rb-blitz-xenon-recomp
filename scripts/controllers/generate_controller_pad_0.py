"""Render an Xbox Series pad texture from its pad art and adjustable callout routes.

Run: python scripts/controllers/generate_controller_pad_0.py
Edit masters/ps3-arrow.svg for the reusable head and colors, and
masters/xbox-series/pad_N-arrows.svg for each pad's routes. The original Xbox
360 pads fix the outer ends under game text.
"""

from pathlib import Path
import math
from xml.etree import ElementTree

import cv2
import numpy as np
from PIL import Image, ImageChops, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/controller/masters/xbox-series/pad.png"
HEAD_SVG = ROOT / "assets/controller/masters/ps3-arrow.svg"
SIZE = 1024
SCALE = 4
SVG = "{http://www.w3.org/2000/svg}"
EXPECTED_ROUTES = (9, 8, 5, 7)
EXPECTED_RAILS = (5, 4, 5, 5)

# Body fit measured against template-360/pads/pad_0.png. Its original silhouette
# occupies roughly x=144..880, y=304..751. This fit puts the Series pad's
# visible silhouette at about x=150..875, y=290..755.
BODY = (100, 245, 924, 798)

def read_layout(pad_index: int, layout_id: str = "xbox-series",
                expected_routes: int | None = None) -> tuple[dict[str, tuple[tuple[int, int], ...]],
                                                             tuple[tuple[int, int, int], ...], int]:
    """Read the editable SVG polylines and fixed text rails."""
    layout_svg = ROOT / f"assets/controller/masters/{layout_id}/pad_{pad_index}-arrows.svg"
    root = ElementTree.parse(layout_svg).getroot()
    if root.get("viewBox") != "0 0 1024 1024":
        raise ValueError(f"{layout_svg.name} must use the 1024-pixel pad canvas")
    route_group = root.find(f"{SVG}g[@id='routes']")
    rail_group = root.find(f"{SVG}g[@id='text-rails']")
    if route_group is None or rail_group is None:
        raise ValueError(f"Missing routes or text-rails group in {layout_svg.name}")
    width = int(route_group.get("stroke-width", "14"))
    if width not in (13, 14):
        raise ValueError(f"Unsupported arrow width in {layout_svg.name}: {width}")
    routes = {}
    for polyline in route_group.findall(f"{SVG}polyline"):
        name = polyline.get("id")
        points = tuple(tuple(map(int, pair.split(",")))
                       for pair in polyline.get("points", "").split())
        if not name or name in routes or len(points) < 2:
            raise ValueError(f"Invalid arrow route {name!r}")
        routes[name] = points
    expected = EXPECTED_ROUTES[pad_index] if expected_routes is None else expected_routes
    if len(routes) != expected:
        raise ValueError(f"Expected {expected} arrows in {layout_svg.name}, found {len(routes)}")
    rails = []
    for line in rail_group.findall(f"{SVG}line"):
        x1, y1, x2, y2 = (int(line.get(key)) for key in
                          ("x1", "y1", "x2", "y2"))
        if y1 != y2 or x1 >= x2:
            raise ValueError(f"Text rail {line.get('id')} must be horizontal")
        rails.append((x1, x2, y1))
    if len(rails) != EXPECTED_RAILS[pad_index]:
        raise ValueError(f"Expected {EXPECTED_RAILS[pad_index]} text rails in {layout_svg.name}, found {len(rails)}")
    return routes, tuple(rails), width


def route_distance(first: tuple[tuple[int, int], tuple[int, int]],
                   second: tuple[tuple[int, int], tuple[int, int]]) -> float:
    """Minimum centerline gap between two straight callout branches."""
    a, b = first
    c, d = second

    def cross(p: tuple[int, int], q: tuple[int, int], r: tuple[int, int]) -> int:
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])

    ab_c, ab_d = cross(a, b, c), cross(a, b, d)
    cd_a, cd_b = cross(c, d, a), cross(c, d, b)
    collinear = ab_c == ab_d == cd_a == cd_b == 0
    overlaps = (max(min(a[0], b[0]), min(c[0], d[0])) <=
                min(max(a[0], b[0]), max(c[0], d[0])) and
                max(min(a[1], b[1]), min(c[1], d[1])) <=
                min(max(a[1], b[1]), max(c[1], d[1])))
    if (not collinear or overlaps) and ab_c * ab_d <= 0 and cd_a * cd_b <= 0:
        return 0.0

    def point_to_segment(p: tuple[int, int], start: tuple[int, int],
                         end: tuple[int, int]) -> float:
        dx, dy = end[0] - start[0], end[1] - start[1]
        t = max(0.0, min(1.0, ((p[0] - start[0]) * dx +
                                 (p[1] - start[1]) * dy) / (dx * dx + dy * dy)))
        return math.hypot(p[0] - start[0] - t * dx,
                          p[1] - start[1] - t * dy)

    return min(point_to_segment(a, c, d), point_to_segment(b, c, d),
               point_to_segment(c, a, b), point_to_segment(d, a, b))


def validate_routes(routes: dict[str, tuple[tuple[int, int], ...]],
                    allowed_joins: frozenset[frozenset[str]] = frozenset()) -> None:
    # A shaft is about 20 px wide. Require visible space between each pair;
    # the shared horizontal rails are intentionally excluded from this check.
    route_items = list(routes.items())
    for index, (name, points) in enumerate(route_items):
        for other_name, other_points in route_items[index + 1:]:
            if frozenset((name, other_name)) in allowed_joins:
                continue
            gap = min(route_distance(first, second)
                      for first in zip(points, points[1:])
                      for second in zip(other_points, other_points[1:]))
            if gap < 24:
                raise ValueError(f"{name} and {other_name} collide ({gap:.1f} px)")


def validate_complete(name: str, image: Image.Image,
                      points: tuple[tuple[int, int], ...]) -> None:
    alpha = image.getchannel("A")
    for bend, tip in zip(points, points[1:]):
        for step in range(1, 101):
            t = step / 100
            x = round(bend[0] + (tip[0] - bend[0]) * t)
            y = round(bend[1] + (tip[1] - bend[1]) * t)
            if not alpha.getpixel((x, y)):
                raise ValueError(f"{name} has a gap near ({x}, {y})")


def load_transparent_body(source: Image.Image) -> Image.Image:
    """Use the source artwork's alpha, including its antialiased edges."""
    body = source.convert("RGBA")
    if body.getchannel("A").getextrema()[0] == 255:
        raise ValueError("Expected pad.png with a transparent background")
    return body


def read_head_library() -> tuple[Image.Image, tuple[tuple[int, int, int], ...]]:
    """Read the SVG head and its sampled PS3 cyan depth colors."""
    root = ElementTree.parse(HEAD_SVG).getroot()
    polygon = root.find(f".//{SVG}polygon[@id='cyan-head']")
    metadata = root.find(f"{SVG}metadata[@id='raster-depth-profile']")
    if polygon is None or metadata is None or not metadata.text:
        raise ValueError("ps3-arrow.svg lacks the cyan head or depth profile")
    points = [tuple(map(float, pair.split(",")))
              for pair in polygon.get("points", "").split()]
    if len(points) < 3:
        raise ValueError("ps3-arrow.svg needs at least three head points")
    tile = Image.new("L", (80 * SCALE, 80 * SCALE))
    ImageDraw.Draw(tile).polygon(
        [(round((40 + x) * SCALE), round((40 + y) * SCALE))
         for x, y in points], fill=255)
    swatches = metadata.text.split()
    if len(swatches) != 7 or any(len(value) != 7 or not value.startswith("#")
                                 for value in swatches):
        raise ValueError("PS3 cyan depth profile must contain seven hex colors")
    profile = tuple(tuple(bytes.fromhex(value[1:])) for value in swatches)
    return tile, profile


def rounded_route(points: tuple[tuple[int, int], ...],
                  rails: tuple[tuple[int, int, int], ...]) -> list[tuple[float, float]]:
    """Ease a branch off its rail and round any intermediate elbow."""
    def direction(first: tuple[float, float],
                  second: tuple[float, float]) -> tuple[float, float]:
        dx, dy = second[0] - first[0], second[1] - first[1]
        length = math.hypot(dx, dy)
        return dx / length, dy / length

    def curve(start: tuple[float, float], control: tuple[float, float],
              end: tuple[float, float]) -> list[tuple[float, float]]:
        return [((1 - t) ** 2 * start[0] + 2 * (1 - t) * t * control[0] + t * t * end[0],
                 (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * control[1] + t * t * end[1])
                for t in (step / 16 for step in range(1, 17))]

    start = points[0]
    rail = next((rail for rail in rails
                 if rail[2] == start[1] and rail[0] <= start[0] <= rail[1]), None)
    if rail is None:
        raise ValueError(f"Arrow starts outside a text rail: {start}")
    rail_direction = 1 if points[1][0] > start[0] else -1
    rail_radius = 7
    outgoing = direction(start, points[1])
    before = (start[0] - rail_direction * rail_radius, float(start[1]))
    after = (start[0] + outgoing[0] * rail_radius,
             start[1] + outgoing[1] * rail_radius)
    path = [before, *curve(before, start, after)]

    for index in range(1, len(points) - 1):
        elbow = points[index]
        elbow_radius = 5
        incoming = direction(points[index - 1], elbow)
        outgoing = direction(elbow, points[index + 1])
        before = (elbow[0] - incoming[0] * elbow_radius,
                  elbow[1] - incoming[1] * elbow_radius)
        after = (elbow[0] + outgoing[0] * elbow_radius,
                 elbow[1] + outgoing[1] * elbow_radius)
        path.append(before)
        path.extend(curve(before, elbow, after))

    tip = points[-1]
    approach = direction(points[-2], tip)
    path.append((tip[0] - approach[0] * 18, tip[1] - approach[1] * 18))
    return path


def smooth_elbows(shape: Image.Image,
                  routes: dict[str, tuple[tuple[int, int], ...]]) -> Image.Image:
    """Round only the silhouette within a few pixels of each elbow."""
    pixels = np.asarray(shape).copy()
    margin = 24 * SCALE
    radius = 16 * SCALE
    for points in routes.values():
        for x, y in points[:-1]:
            cx, cy = x * SCALE, y * SCALE
            x0, x1 = max(0, cx - margin), min(SIZE * SCALE, cx + margin + 1)
            y0, y1 = max(0, cy - margin), min(SIZE * SCALE, cy + margin + 1)
            original = pixels[y0:y1, x0:x1]
            blurred = cv2.GaussianBlur(original, (0, 0), sigmaX=2 * SCALE)
            smoothed = np.where(blurred >= 128, 255, 0).astype(np.uint8)
            circle = np.zeros(original.shape, dtype=np.uint8)
            cv2.circle(circle, (cx - x0, cy - y0), radius, 255, thickness=-1)
            pixels[y0:y1, x0:x1] = np.where(circle != 0, smoothed, original)
    return Image.fromarray(pixels)


def render_callouts(routes: dict[str, tuple[tuple[int, int], ...]],
                    rails: tuple[tuple[int, int, int], ...], width: int) -> Image.Image:
    """Join all cyan shapes before adding one outline and the PS3 color profile."""
    high_size = SIZE * SCALE
    shape = Image.new("L", (high_size, high_size))
    draw = ImageDraw.Draw(shape)
    width *= SCALE
    base_head, profile = read_head_library()

    for points in routes.values():
        path = rounded_route(points, rails)
        draw.line(tuple((round(x * SCALE), round(y * SCALE)) for x, y in path),
                  fill=255, width=width, joint="curve")

        new_angle = math.atan2(points[-1][1] - points[-2][1],
                               points[-1][0] - points[-2][0])
        head = base_head.rotate(-math.degrees(new_angle),
                                resample=Image.Resampling.BICUBIC)
        tip = points[-1]
        x, y = (tip[0] - 40) * SCALE, (tip[1] - 40) * SCALE
        region = shape.crop((x, y, x + 80 * SCALE, y + 80 * SCALE))
        shape.paste(ImageChops.lighter(region, head), (x, y))

    draw = ImageDraw.Draw(shape)
    for x_start, x_end, y in rails:
        draw.line((x_start * SCALE, y * SCALE,
                   x_end * SCALE, y * SCALE),
                  fill=255, width=width)

    # Dilate at four times the output size so diagonal black edges are smooth.
    shape = smooth_elbows(shape, routes)
    high_fill = np.asarray(shape)
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE,
                                        (6 * SCALE + 1, 6 * SCALE + 1))
    high_outline = cv2.dilate(high_fill, kernel)
    fill = np.asarray(shape.resize((SIZE, SIZE), Image.Resampling.LANCZOS))
    outline = np.asarray(Image.fromarray(high_outline).resize(
        (SIZE, SIZE), Image.Resampling.LANCZOS))
    components, _ = cv2.connectedComponents((fill >= 128).astype(np.uint8))
    if components - 1 != len(rails):
        raise ValueError(f"Expected {len(rails)} complete arrow groups, found {components - 1}")

    # The original PS3 cyan is pale at the rim and deepens toward the center.
    distance = cv2.distanceTransform((fill >= 128).astype(np.uint8),
                                      cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    stops = (0, 1, 2, 3, 4, 5, 6)
    colors = np.stack([np.interp(distance, stops, [rgb[channel] for rgb in profile])
                       for channel in range(3)], axis=-1).astype(np.uint8)
    result = Image.new("RGBA", (SIZE, SIZE))
    result.putalpha(Image.fromarray(outline))
    result.alpha_composite(Image.fromarray(np.dstack((colors, fill)), "RGBA"))
    return result


def generate_pad(pad_index: int) -> None:
    if pad_index not in range(4):
        raise ValueError(f"Unknown Xbox Series pad index: {pad_index}")
    routes, rails, width = read_layout(pad_index)
    validate_routes(routes)
    with Image.open(SOURCE) as source:
        body = load_transparent_body(source)
    art = Image.new("RGBA", (SIZE, SIZE))
    fitted = body.resize((BODY[2] - BODY[0], BODY[3] - BODY[1]),
                         Image.Resampling.LANCZOS)
    art.alpha_composite(fitted, BODY[:2])
    callouts = render_callouts(routes, rails, width)
    for name, points in routes.items():
        validate_complete(name, callouts, points)
    alpha = callouts.getchannel("A")
    for x_start, x_end, y in rails:
        if any(not alpha.getpixel((x, y)) for x in range(x_start, x_end + 1)):
            raise ValueError(f"Horizontal arrow rail has a gap at y={y}")
    art.alpha_composite(callouts)
    dest = ROOT / f"assets/controller/xbox-series/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    generate_pad(0)


if __name__ == "__main__":
    main()
