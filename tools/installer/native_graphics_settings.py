"""Native graphics choices and exact render sizing, independent of ReShade.

Validation is not qualification: a launch must separately verify its payload,
edition, renderer, and evidence-backed feature availability.
"""
DEFAULTS = dict(render_scale=100, improved_water=False, improved_terrain=False,
                improved_vegetation=False, stable_shadows=False)
SCALES = (100, 150, 200)
MAX_WIDTH, MAX_HEIGHT = 3840, 2160


def validate(value):
    if value is None:
        return dict(DEFAULTS)
    if not isinstance(value, dict) or set(value) != set(DEFAULTS):
        raise ValueError('native_graphics_invalid')
    if type(value['render_scale']) is not int or value['render_scale'] not in SCALES:
        raise ValueError('native_graphics_invalid')
    if any(type(value[key]) is not bool for key in DEFAULTS if key != 'render_scale'):
        raise ValueError('native_graphics_invalid')
    return dict(value)


def requested_features(value):
    value = validate(value)
    return tuple((['supersampling'] if value['render_scale'] != 100 else []) +
                 [key for key in DEFAULTS if key != 'render_scale' and value[key]])


def require_features(value, available):
    """Unsupported selections fail explicitly; never silently claim activation."""
    features = requested_features(value)
    if not isinstance(available, (tuple, list, set, frozenset)):
        raise ValueError('native_graphics_unavailable')
    if not set(features) <= set(available):
        raise ValueError('native_graphics_unavailable')
    return features


def render_size(value, display):
    value = validate(value)
    if not requested_features(value):
        return None
    if (not isinstance(display, dict) or not {'width', 'height', 'mode'} <= set(display)
            or display['mode'] not in ('windowed', 'borderless')):
        raise ValueError('native_graphics_display_required')
    width, height = display['width'], display['height']
    if (type(width) is not int or type(height) is not int
            or not 800 <= width <= MAX_WIDTH or not 600 <= height <= MAX_HEIGHT):
        raise ValueError('native_graphics_resolution_invalid')
    scale = value['render_scale']
    if width * scale % 100 or height * scale % 100:
        raise ValueError('native_graphics_resolution_invalid')
    render_width, render_height = width * scale // 100, height * scale // 100
    if render_width > MAX_WIDTH or render_height > MAX_HEIGHT:
        raise ValueError('native_graphics_resolution_limit')
    return dict(output_width=width, output_height=height,
                render_width=render_width, render_height=render_height,
                axis_percent=scale, pixel_multiplier=scale * scale / 10000)
