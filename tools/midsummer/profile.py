"""Generate a bright looping environment from the recipient's verified assets.

No game XML is distributed. Install the same resource on every peer; choosing
the environment remains a local graphics preference, just like stock weather.
"""
import copy
import hashlib
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'lan'))
from bba import Archive
from archive_append import append_resource

SOURCE_SHA256 = '30d679720e14ec9007f359c5703d854b78a866480f856810c934e439d620e3ed'
RESOURCE = 'Config/Environment/WePlayMidsummer.xml'
PERIOD_MS = 1200000
# Twilight -> day -> twilight, repeated. Elevation stays above the horizon.
# HDR exposure remains well above the stock night value (about 0.196).
CURVES = {
    'SunElevation': (14.0, 48.0), 'SunFXElevation': (14.0, 48.0),
    'SunDiffuseColor': ((.88, .72, .55, 1.), (.80, .86, .95, 1.)),
    'SunDiffuseColorMultiplier': (1.05, 1.20), 'HDRExposure': (2.6, 3.05),
    'TerrainAmbientColorMultiplier': (1.12, 1.0), 'ShadowFactor': (.19, .25),
    'WindowLight': (.2, 0.), 'Rain': (0., 0.), 'Thunderstorm': (0., 0.),
    'SkyColor': ((.70, .55, .52, 1.), (.54, .68, .87, 1.)),
    'SunShaftColor': ((1., .75, .50, 1.), (.80, .91, 1., 1.)),
}


def environment(source, *, period_ms=PERIOD_MS):
    """Keep unrelated wind, water, vegetation and atmosphere curves intact."""
    if type(period_ms) is not int or not 1000 <= period_ms <= 3600000:
        raise ValueError('Invalid environment period')
    if not isinstance(source, bytes) or len(source) > 1024 * 1024:
        raise ValueError('Invalid environment XML')
    end = source.find(b'</EnvironmentSet>')
    if end < 0 or b'<!DOCTYPE' in source or b'<!ENTITY' in source:
        raise ValueError('Invalid environment document')
    root = ET.fromstring(source[:end + len(b'</EnvironmentSet>')])
    if root.tag != 'EnvironmentSet' or root.findtext('Duration') != '1200000.':
        raise ValueError('Unknown environment schema')
    root.find('Name').text = 'WePlayMidsummer'
    root.find('Duration').text = str(period_ms)
    for item in root:
        for field in ('Duration', 'CurrentTime', 'LastUpdatetTime', 'StartTime'):
            value = item.find(field)
            if value is not None:
                value.text = str(period_ms if field == 'Duration' else 0)
        for time in item.findall('./KeyFrames/Key/Time'):
            time.text = format(float(time.text) * period_ms / PERIOD_MS, '.9g')
    for name, values in CURVES.items():
        item = root.find(name)
        if item is None or item.find('Loop') is None or item.find('KeyFrames/Key') is None:
            raise ValueError('Missing environment curve: ' + name)
        item.find('Loop').text = 'true'
        keys = item.find('KeyFrames')
        sample = copy.deepcopy(keys[0])
        keys.clear()
        for phase, value in ((0., values[0]), (.5, values[1]), (1., values[0])):
            key = copy.deepcopy(sample)
            key.find('Time').text = format(phase * period_ms, '.9g')
            target = key.find('Value')
            leaves = [e for e in target.iter() if not len(e) and e.tag != 'BlockVersion']
            numbers = value if isinstance(value, tuple) else (value,)
            if len(leaves) != len(numbers):
                raise ValueError('Unknown environment value: ' + name)
            for leaf, number in zip(leaves, numbers):
                leaf.text = format(number, '.9g')
            for tag in ('TangentToPrev', 'TangentToNext'):
                tangent = key.find(tag)
                if tangent is not None:
                    for leaf in tangent.iter():
                        if not len(leaf) and leaf.tag != 'BlockVersion':
                            leaf.text = '0.'
            keys.append(key)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def build(raw):
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError('Mittsommer braucht das verifizierte History-Ressourcenarchiv.')
    archive = Archive(raw)
    return append_resource(raw, RESOURCE, environment(archive.read('Config/Environment/Neutral.xml')))
