"""Experimental Gold-only child-local ReShade profile, never global layers.

Uses the existing integrity/preset machinery in a separate module instance;
History runtime pins, folders and installer admission remain unchanged.
"""
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'installer'))
# The History graphics machinery imports tools/lan/prepare.py by its legacy
# name. Keep Gold's preparation module out of that shared import namespace.
gold_spec = importlib.util.spec_from_file_location('gold_visual_prepare', HERE / 'prepare.py')
gold = importlib.util.module_from_spec(gold_spec)
gold_spec.loader.exec_module(gold)

EXE_SHA256 = '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab'
CLOCK_SHA256 = '60e8f3d4acaf5240b9c034c843c9bbc33954bf629c6c01b70388de3318a1d4fe'
spec = importlib.util.spec_from_file_location('gold_reshade_runtime', HERE.parent / 'installer/reshade_runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
runtime.CLOCK_SHA256 = CLOCK_SHA256
runtime.CLOCK_FILE = 'Runtime/Graphics/WePlayGoldAtmosphere.addon32'
runtime.FOLDER = 'LocalData/ReShade-Gold-Visual-v1'


def checked_root(root):
    root = gold.plain(root)
    executable = gold.plain(root / gold.EXE)
    if gold.digest(executable) != EXE_SHA256:
        raise ValueError('Gold visual runtime requires the exact prepared Gold executable.')
    if gold.digest(executable.with_name('d3d9.dll')) != gold.DXVK_SHA256:
        raise ValueError('Gold visual runtime requires pinned Modern graphics.')
    return root


def _checked_options(options, allow_unqualified_maximum):
    # Match the ordinary Gold launcher: this experimental frame-history path
    # has reported UI ghosting and must not slip into routine pair tests.
    if runtime._options(options)[3] and allow_unqualified_maximum is not True:
        raise ValueError('Gold Maximum is unqualified because of UI ghosting; use the Modern SMAA profile.')
    return options


def prepare(root, *, allow_unqualified_maximum=False, **kwargs):
    _checked_options(kwargs.get('options'), allow_unqualified_maximum)
    return runtime.prepare(checked_root(root), **kwargs)


def verify(root, options=None, *, allow_unqualified_maximum=False):
    _checked_options(options, allow_unqualified_maximum)
    return runtime.verify(checked_root(root), options)


def launch_environment(root, inherited, options=None, *, allow_unqualified_maximum=False):
    _checked_options(options, allow_unqualified_maximum)
    return runtime.launch_environment(checked_root(root), inherited, options)
