"""Explicit host boundaries; never run Windows helpers under Wine."""
import os
from pathlib import Path
import sys


def server_command(root):
    root = Path(root)
    if os.name == 'nt':
        return [str(root / 'Runtime/dotnet/dotnet.exe'), str(root / 'Server/S7Patcher.dll')]
    return [str(root / 'Server/S7Patcher')]


def python_command(root):
    return str(Path(root) / 'Runtime/python/python.exe') if os.name == 'nt' else sys.executable


def crypto_command(root=None):
    if root is None:
        path = Path(__file__).resolve()
        root = path.parents[2] if path.parent.name == 'installer' else path.parent.parent
    root = Path(root)
    if os.name == 'nt':
        return root / 'Runtime/dotnet/WePlay.WindowsHelper.exe'
    return root / ('Server' if (root / 'Server').is_dir() else 'Payload/Server') / 'WePlay.LinuxHelper'


def game_command(root, arguments, environment):
    if os.name == 'nt':
        return [str(Path(root) / 'Base/Bin/Settlers7R.exe'), *arguments], environment
    import linux_runtime
    return linux_runtime.game_command(root, arguments, environment)
