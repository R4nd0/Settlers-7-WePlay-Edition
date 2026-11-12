"""Reviewed native LuaJIT API maps; unknown builds always fail closed."""
UBISOFT = dict(newstate=0x458170, close=0x44bf90, sethook=0x445ea0,
               getstack=0x444ce0, getinfo=0x45af70, getlocal=0x44cfc0,
               settop=0x44c570, load=0x4714d0, reader=0x44b3c0, pcall=0x446500)
STEAM = dict(newstate=0x458180, close=0x44bfa0, sethook=0x445eb0,
             getstack=0x444cf0, getinfo=0x45af80, getlocal=0x44cfd0,
             settop=0x44c580, load=0x4714e0, reader=0x44b3d0, pcall=0x446510)
GOLD = dict(newstate=0xbe1550, close=0xbd4910, sethook=0xbcd6d0,
            getstack=0xbcc360, getinfo=0xbe45b0, getlocal=0xbd59e0,
            settop=0xbd4f00, load=0xbfb350, reader=0xbd3a50, pcall=0xbce030)
PROFILES = {
    '6dd278274aa885ff402a277f6d2f3ba05600e1ff841462e038f44bf233ce992f': ('history-ubisoft', 'A', UBISOFT),
    'dbc108237301f329fed5372f79915c82acd9085f58d32adefa4efe434208ef72': ('history-ubisoft', 'B', UBISOFT),
    'cf7f5f25665d8e98ff3b17a70e658be4458f67b29c42455c83715a4bb68c7d2d': ('history-steam', 'C', STEAM),
    'ce9f64801cd6e837df287e9a5c180d653f4e14814cd2806d2cb8b898106fc810': ('history-steam', 'D', STEAM),
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab': ('gold', None, GOLD),
}


def select(pin):
    if pin not in PROFILES:
        raise ValueError('Unsupported executable; expected a fingerprinted isolated S7 build')
    version, slot, api = PROFILES[pin]
    return dict(version=version, slot=slot, api=dict(api))
