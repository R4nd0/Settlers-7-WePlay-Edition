"""Strict laboratory sizing plan; does not start/configure S7 or qualify its UI."""
import argparse
from dataclasses import asdict, dataclass
import json

MAX_WIDTH = 3840
MAX_HEIGHT = 2160
MAX_PIXELS = MAX_WIDTH * MAX_HEIGHT
SCALES = {'1': (1, 1), '1.5': (3, 2), '2': (2, 1)}


@dataclass(frozen=True)
class Plan:
    output_width: int
    output_height: int
    render_width: int
    render_height: int
    axis_scale: str
    pixel_multiplier: float
    color_surface_bytes: int
    game_qualified: bool = False


def create(width, height, scale):
    if (type(width) is not int or type(height) is not int
            or not 800 <= width <= MAX_WIDTH or not 600 <= height <= MAX_HEIGHT
            or type(scale) is not str or scale not in SCALES):
        raise ValueError('Use an 800x600 to 3840x2160 output and scale 1, 1.5 or 2.')
    numerator, denominator = SCALES[scale]
    if width * numerator % denominator or height * numerator % denominator:
        raise ValueError('The exact per-axis scale requires integral render dimensions.')
    render_width = width * numerator // denominator
    render_height = height * numerator // denominator
    if (render_width > MAX_WIDTH or render_height > MAX_HEIGHT
            or render_width * render_height > MAX_PIXELS):
        raise ValueError('Render resolution exceeds the initial 3840x2160 laboratory cap.')
    return Plan(width, height, render_width, render_height, scale,
                numerator * numerator / (denominator * denominator),
                render_width * render_height * 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    parser.add_argument('--scale', choices=SCALES, required=True,
                        help='Per-axis scale; 1.5 means 2.25 times as many pixels.')
    args = parser.parse_args()
    try:
        plan = create(args.width, args.height, args.scale)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(asdict(plan), indent=2))


if __name__ == '__main__':
    main()
