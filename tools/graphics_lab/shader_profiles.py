"""Closed native shader-profile setup data; no filesystem/process/GPU access.

The caller must verify its actual build capabilities and use the named guarded
recipient stager. These records neither grant capabilities nor enable effects.
"""
from copy import deepcopy

WATER, LEAF, TERRAIN, SHADOW, AO, ALL = 1, 2, 3, 4, 5, 6
PROFILE_IDS = {'water': WATER, 'vegetation': LEAF, 'terrain': TERRAIN, 'shadow': SHADOW, 'ambient': AO, 'all': ALL}

LEAF_PROGRAMS = (
    ('A', 'leaf-palette-A-v5', '1b825358f46c39b04b1d540738f4be5e862bfe6627b8545ab092c471d259477c'),
    ('B', 'leaf-palette-B-v6', '284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a'),
    ('C', 'leaf-palette-C-v7', 'b869fe375b76509f60a48d34bde83565d28c0416aca85f27c8a445045cb6a51f'),
)

def _choice(row, label, **request):
    return {'row': row, 'label': label, 'request': request}

_PROFILES = {
    WATER: dict(key='water', title='Wasser', adapter='water_palette', wire_bytes=3096, config_version=2,
        stager='tools.material_shaders.water_palette_staging', required_abi={'water_palette': 3096},
        choices=(_choice(0, 'Original', enabled=0, selected_variant=0),
                 _choice(1, 'Wasser B', enabled=1, selected_variant=0),
                 _choice(2, 'Klareres Ufer', enabled=1, selected_variant=1),
                 _choice(3, 'Mehr Glanz', enabled=1, selected_variant=2)),
        expected_effect='Abgenommenes Wasser B und zwei feste Reflexions-/Tiefenalternativen vergleichen.',
        scene='Burgszene: Fluss, linkes Ufer und nahes flaches Wasser bei normalem Zoom.',
        visual_status='Nur Wasser B ist optisch abgenommen; die Alternativen bleiben offen.'),
    LEAF: dict(key='vegetation', title='Blattlicht', adapter='material', wire_bytes=4120, config_version=4,
        stager='tools.material_shaders.staging_leaf_palette', required_abi={'effects_v4': 4120},
        declared_material_flags=4, choices=(_choice(0, 'Original', flags=0, selected_variant=0),
            _choice(1, 'Blatt A (schwach)', flags=4, selected_variant=0),
            _choice(2, 'Blatt B (mittel)', flags=4, selected_variant=1),
            _choice(3, 'Blatt C (stark)', flags=4, selected_variant=2)),
        expected_effect='Blattseiten stufenweise heller und besser lesbar; Stark kann Lichtkontrast abflachen.',
        scene='Burgszene: vordere Baumgruppen und linke Baumkrone bei normalem Zoom; Wind mitbeurteilen.',
        visual_status='Drei echte Pixelprogramme vorhanden; keine optische Freigabe der Palette.',
        fixed_curves=((.8, .2), (2/3, 1/3), (4/7, 3/7)),
        programs=tuple(dict(index=i, name=name, recipe=recipe, sha256=digest, bytes=1372)
                       for i, (name, recipe, digest) in enumerate(LEAF_PROGRAMS)),
        original_vertex_preserved=True, hemi_stack=False),
    TERRAIN: dict(key='terrain', title='Gelaende', adapter='material', wire_bytes=6180, config_version=2,
        stager='tools.material_shaders.staging_macro', required_abi={'effects_v2': 6180},
        declared_material_flags=2, choices=(_choice(0, 'Original', flags=0), _choice(1, 'Makro-Relief (Profil)', flags=2)),
        expected_effect='Groessere bestehende Detailstrukturen und laenger erhaltene Nahdetails; keine globale Farbkorrektur.',
        scene='Burgszene: beleuchteter Boden, Wege und geneigte Uferflaechen im Vordergrund bei normalem Zoom.',
        visual_status='Bestehender Makro-Versuch zeigte bislang keinen ueberzeugenden Gewinn im Normalblick.',
        terrain_recipe_variant=3, terrain_detail_scale=1.0, terrain_relief_scale=1.0,
        detail_uv_frequency_scale=.5, near_distances={'bump': (12000., 18000.), 'parallax': (8000., 12800.)},
        independent_detail_relief_choices=False,
        reason='Makro-Vertrag verlangt beide Skalen exakt1.0 und erlaubt live nur flags0/2; Detail/Relief sind keine weiteren freigegebenen Modi.'),
    SHADOW: dict(key='shadow', title='Schatten', adapter='shadow', wire_bytes=12, config_version=1,
        stager=None, required_abi={}, required_diagnostic_abi={'shadow': 12},
        choices=(_choice(0, 'Original', enabled=0, profile_version=1), _choice(1, 'Schattenfilter AN', enabled=1, profile_version=1)),
        expected_effect='Vorhandenen nativen Schattenfilter am passenden Schattenpass vergleichen.',
        scene='Burgszene: Schattenkanten an Mauern und Gebaeuden bei normalem Zoom.',
        visual_status='Bisher keine optische Freigabe des Schattenversuchs.'),
    AO: dict(key='ambient', title='Kontaktkontrast', adapter='ambient', wire_bytes=12, config_version=2,
        stager=None, required_abi={}, required_diagnostic_abi={'ambient_v2': 12},
        choices=(_choice(0, 'Original', enabled=0, profile_version=2),
                 _choice(1, 'AO A (Grenze 1,00)', enabled=1, profile_version=1),
                 _choice(2, 'AO B (Grenze 1,25)', enabled=1, profile_version=2)),
        expected_effect='Vorhandene Tiefen-/Normalen-SSAO begrenzt verstaerken; Wirkung kann im Normalblick sehr subtil bleiben.',
        scene='Burgszene: Bodenanschluesse von Mauern, Gebaeuden und Vegetation.',
        visual_status='AO B war sehr subtil; keine optische Freigabe. A kann Originalwerte ueber1 ablehnen.',
        policies=({'profile_version': 1, 'original_max': 1.0, 'gain': 1.25, 'ceiling': 1.0},
                  {'profile_version': 2, 'original_max': 1.25, 'gain': 1.25, 'ceiling': 1.25})),
    ALL: dict(key='all', title='Alle Shadergruppen', adapter='combined', wire_bytes=24, config_version=5,
        stager='tools.material_shaders.staging_combined', required_abi={'effects_v5':24,'initialize_v3':3136},
        required_diagnostic_abi={'overlay_v3':16}, declared_material_flags=6, choices=(),
        groups=('water','vegetation','terrain','shadow','ambient'),
        expected_effect='Alle vorhandenen Gruppen in einem Prozess getrennt waehlen und gemeinsam beurteilen; keine neuen Optiken.',
        scene='Burgszene: Fluss, vordere Baumgruppen, Boden und Schatten-/Kontaktkanten bei normalem Zoom.',
        visual_status='Nur Wasser B ist optisch abgenommen; ein kombiniertes Release-Preset bleibt unqualifiziert.',
        initial_group_requests={'water':{'enabled':1,'selected_variant':0},'vegetation':{'flags':0,'selected_variant':0},
            'terrain':{'flags':0},'shadow':{'enabled':0,'profile_version':1},'ambient':{'enabled':0,'profile_version':2}}),
}

_RUN_KWARGS = {
    WATER: dict(water_program=True, water_palette=True, water_variant='B',
                material_effects=('improved_water',), material_config_version=1, effects_initially_off=False),
    LEAF: dict(leaf_palette=True, material_effects=('improved_vegetation',),
               material_config_version=4, effects_initially_off=True),
    TERRAIN: dict(terrain_macro=True, material_effects=('improved_terrain',),
                  material_config_version=2, effects_initially_off=True),
    SHADOW: dict(shadow_filter=True, material_effects=(), material_config_version=1, effects_initially_off=True),
    AO: dict(ambient_occlusion=True, ambient_profile='B', material_effects=(),
             material_config_version=1, effects_initially_off=True),
    ALL: dict(water_program=True, water_palette=True, water_variant='B',terrain_macro=True,leaf_palette=True,
              shadow_filter=True,ambient_occlusion=True,ambient_profile='B',
              material_effects=('improved_water','improved_terrain','improved_vegetation'),
              material_config_version=5,effects_initially_off=True),
}
for _number, _record in _PROFILES.items():
    _record['run_kwargs'] = dict(high_quality=True, shader_overlay=True, shader_profile=_record['key'], **_RUN_KWARGS[_number])
    _record['required_diagnostic_abi'] = {**({'overlay_v2':16}if _number!=ALL else {}), **_record.get('required_diagnostic_abi', {})}

# Public setup snapshots for the guarded command runner. profile() returns a
# fresh copy so caller changes cannot mutate the internal closed definitions.
PROFILES = {record['key']: {'id': number, **deepcopy(record)} for number, record in _PROFILES.items()}

def profile_id(value):
    if type(value) is int and value in _PROFILES:
        return value
    if type(value) is str and value in PROFILE_IDS:
        return PROFILE_IDS[value]
    raise ValueError('Select water, vegetation, terrain, shadow, ambient or explicit combined all.')

def profile(value):
    number = profile_id(value)
    return {'id': number, **deepcopy(_PROFILES[number])}

def profiles():
    return [profile(number) for number in range(1, 7)]

def choice(value, row):
    record = profile(value)
    if type(row) is not int or not 0 <= row < len(record['choices']):
        raise ValueError('This shader family has no such fixed choice.')
    return record['choices'][row]

def setup(value, *, render_scale=200, initially_off=None):
    """Describe one isolated run; callers still own guarded staging and IO."""
    if type(render_scale) is not int or render_scale not in (100, 150, 200):
        raise ValueError('Unsupported render scale.')
    record = profile(value)
    if initially_off is None:
        initially_off = record['run_kwargs']['effects_initially_off']
    if type(initially_off) is not bool:
        raise ValueError('Explicit initial OFF Boolean required.')
    selected = None if record['id']==ALL else 0 if initially_off else 1
    request={'flags':0 if initially_off else 6,'selected_variant':0}if record['id']==ALL else record['choices'][selected]['request']
    plan = dict(format='s7-native-shader-profile-plan-v1', profile=record, one_family_per_run=record['id']!=ALL,
                render_scale=render_scale, initially_off=initially_off, initial_choice=selected,
                initial_request=deepcopy(request),
                required_shader_quality='high', capability_verified=False, active=False,
                runtime_verified=False, visual_quality_qualified=False)
    plan['run_kwargs'] = {**deepcopy(record['run_kwargs']), 'effects_initially_off': initially_off}
    if record['id'] in (LEAF, TERRAIN, ALL):
        plan['staging_selection'] = dict(render_scale=render_scale, improved_water=False,
            improved_terrain=record['id'] in(TERRAIN,ALL), improved_vegetation=record['id'] in(LEAF,ALL), stable_shadows=False)
        plan['stager_kwargs'] = {'config_version': record['config_version']}
        if record['id'] == LEAF:
            plan['stager_kwargs']['selected_variant'] = record['choices'][selected]['request']['selected_variant']
        if record['id']==ALL:
            plan['stager_kwargs'].update(enabled_flags=request['flags'],selected_variant=0)
            plan['initial_group_requests']=deepcopy(record['initial_group_requests'])
    return plan
