"""Explicit, Gold-only graphics candidate. Normal installer admission stays closed.

Use only on a backed-up, separate test copy at Full HD with the qualified DXVK
descriptor-set configuration. Runtime and add-on bytes must match the actual
tested build; this does not admit arbitrary local rebuilds.
"""
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
ADDON_SHA256 = '2544463b15172e4585c9e25a405ce75ecb309f2dba78f538ef8379d0d9b9c19b'
RESHADE_SHA256 = 'dc4fed1d519f8f6c45c464a26e18d1f1833c8789e41c4165ff7865ab2c49d989'
ARCHIVE_SHA256 = '9d6c2ce5c0308ad23747624e41508cdb3ec93e7947a4aed19ca8f288878d34f0'
ADDON_FILE = 'Runtime/Graphics/WePlayGoldPreUIPublic2.addon32'
DXVK_CONFIG = 'dxvk.enableDescriptorHeap = False; dxvk.enableDescriptorBuffer = False'


def visual():
    # Fresh instances keep experimental pins out of other Gold/History callers.
    spec = importlib.util.spec_from_file_location('gold_pre_ui_visual', HERE / 'visual_runtime.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.runtime.CLOCK_SHA256 = ADDON_SHA256
    module.runtime.CLOCK_FILE = ADDON_FILE
    module.runtime.ADDON_DLL_SHA256 = RESHADE_SHA256
    module.runtime.ADDON_SETUP_SHA256 = ARCHIVE_SHA256
    return module


def prepare(root, *, setup, options):
    return visual().prepare(root, setup=setup, options=options)


def verify(root, options=None):
    return visual().verify(root, options)


def launch_environment(root, inherited, options=None):
    if inherited.get('DXVK_CONFIG') not in (None, '', DXVK_CONFIG):
        raise ValueError('Unqualified inherited DXVK configuration')
    result = visual().launch_environment(root, inherited, options)
    result['DXVK_CONFIG'] = DXVK_CONFIG
    return result
