"""Gold layout adapter for common History choices, excluding defective Maximum."""
import importlib.util
import os
from pathlib import Path

import graphics_profile as graphics
import graphics_choice as choice
import platform_standalone as platform

EXE = 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
INTERFACE = 'Data/Base/BBA/Interface.bba'
INTERFACE_SHA256 = 'de8b63ab6934e2231a8279647414ec9c15422febc6a23d1aa8bb3485db657635'
OPTIONS = ('midsummer_tone','enhanced_aa','contact_shadows','maximum_fx')


def is_gold(root):
    return graphics._plain(Path(root)/EXE).is_file() or graphics._plain(Path(root)/'gold-install.json').exists()


def checked_root(root):
    root=graphics._plain(root)
    value,exe,_=platform.load(root)
    if value['edition']!='gold':raise ValueError('gold_graphics_invalid')
    record=graphics._json(graphics._read(root/'gold-install.json',graphics.MAX_JSON))
    expected={INTERFACE:INTERFACE_SHA256,EXE:'76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab',
              exe.with_name('d3d9.dll').relative_to(root).as_posix():graphics.DXVK_SHA256}
    if (record.get('format')!='weplay-gold-install-v1' or record.get('multiplayer') is not False
            or record.get('platform_independent') is not True or not isinstance(record.get('files'),dict)
            or (root/'PREPARATION-INCOMPLETE.txt').exists()):raise ValueError('gold_graphics_invalid')
    for name,digest in expected.items():
        if record['files'].get(name)!=digest or graphics.content.digest(graphics._plain(root/name))!=digest:
            raise ValueError('gold_graphics_invalid')
    return root


def validate(root,selected):
    selected=choice.validate_choice(selected)
    if selected.get('maximum_fx',False):
        raise ValueError('gold_maximum_unavailable')
    return selected


def runtime(root,selected):
    validate(root,selected)
    spec=importlib.util.spec_from_file_location('weplay_gold_reshade',Path(__file__).with_name('reshade_runtime.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.FOLDER='LocalData/ReShade-Gold-Visual-v2'
    # Even a non-Maximum Gold profile must never use History's clock/layout.
    module.CLOCK_FILE='Launcher/WePlayGoldAtmosphere.addon32'
    module.CLOCK_SHA256='60e8f3d4acaf5240b9c034c843c9bbc33954bf629c6c01b70388de3318a1d4fe'
    return module


def prepare_effects(root,selected,**kwargs):
    root=checked_root(root);selected=validate(root,selected)
    if any(selected.get(n,False) for n in OPTIONS):return runtime(root,selected).prepare(root,options=selected,**kwargs)


def save_choice(root,selected):
    root=checked_root(root);selected=validate(root,selected)
    if selected.get('display') is not None:
        import display_settings as display
        display.resolve(selected['display'],display.enumerate_displays(),renderer='dxvk',gpu=selected['choice'])
    choice._load_choice(root)
    prepare_effects(root,selected)
    data=graphics._plain(root/'LocalData');data.mkdir(exist_ok=True)
    graphics.atomic_write(data/'graphics-choice.json',graphics.encoded(selected),data)
    return selected


def launch_environment(root,inherited=None):
    root=checked_root(root);selected=validate(root,choice._load_choice(root))
    environment=dict(os.environ if inherited is None else inherited)
    environment={k:v for k,v in environment.items() if not k.upper().startswith(('DXVK_','S7_','WEPLAY_'))}
    environment=choice.visual_environment(selected,environment)
    logs=graphics._plain(root/'LocalData')
    environment.update(DXVK_LOG_PATH=str(logs),DXVK_SHADER_CACHE_PATH=str(logs),DXVK_STATE_CACHE_PATH=str(logs))
    if any(selected.get(n,False) for n in OPTIONS):
        environment=runtime(root,selected).launch_environment(root,environment,options=selected)
    import quality_settings as quality
    if (root/quality.FILE).exists():quality.prepare(root)
    return environment


def launch_options(root):
    selected=validate(root,choice._load_choice(root))
    return dict(device_name=selected['choice'],anisotropic_filtering=selected.get('anisotropic_filtering',False))
