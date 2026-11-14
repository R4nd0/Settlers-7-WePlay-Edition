"""Keep Gold's normal loading UI lifecycle when its optional candle is absent."""
import hashlib
from profile_interface import decoded_view

MEMBER='Script/local/menu/panel/loadingScreen.lua'
SOURCE='f2199925e69ef3acffbb5ec5b3d0471af998727af092ccba48178f0dad3c2605'
CHANGES=(
    ('n["candleProgress"]["create"](n["loadingScreen"]["frame"],e,o)',
     'local weplayCandle=_G.rawget(n,"candleProgress")\n'
     'if weplayCandle then weplayCandle.create(n.loadingScreen.frame,e,o) end\n'),
    ('if n["candleProgress"]["isCreated"]()then\n'
     'n["candleProgress"]["destroy"]()end\nn["candleProgress"]["destroy"]()',
     'local weplayCandle=_G.rawget(n,"candleProgress")\n'
     'if weplayCandle and weplayCandle.isCreated() then weplayCandle.destroy() end\n'),
    ('if n["candleProgress"]["isCreated"]()then\n'
     'n["candleProgress"]["setProgress"](e/100)end',
     'local weplayCandle=_G.rawget(n,"candleProgress")\n'
     'if weplayCandle and weplayCandle.isCreated() then weplayCandle.setProgress(e/100) end'),
)


def patch(raw):
    if hashlib.sha256(raw).hexdigest()!=SOURCE:
        raise ValueError('Unrecognized Gold loading-screen script')
    for before,after in CHANGES:
        source,body,offsets=decoded_view(raw)
        if body.count(before)!=1:
            raise ValueError('Gold loading-screen lifecycle anchor differs')
        start=body.index(before)
        raw=(source[:offsets[start]]+after+source[offsets[start+len(before)]:]).encode('utf-8')
    return raw
