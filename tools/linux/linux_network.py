"""Native Linux multiplayer orchestration using the shared room protocols."""
import json
from pathlib import Path
from types import SimpleNamespace

import compatibility
import game_directory
import launch_auto_lan
import linux_runtime
from ingame_directory import Bridge as DirectoryBridge
from ingame_invitation import Bridge as InvitationBridge, configuration
from public_directory import PublicReporter


def games(root):
    root = compatibility.plain(root)
    expected = compatibility.network_id(compatibility.verify(root))
    url = game_directory.configured(root / 'LocalData')
    if url is None: raise ValueError('directory_not_connected')
    return [dict(row, compatible=row['content_id'] == expected) for row in game_directory.listing(url)]


def run(root, mode, *, player_name='LocalPlayer', consent=False, invitation=None, game_id=None,
        invitation_ready=None, notify=None, cancel_check=None, version='2.0.0-rc3.17'):
    root = compatibility.plain(root)
    # Solo and LAN verify the whole inventory under their session lock before
    # this monitor can open any bridge. Internet joins also need verification
    # here, before calling the cryptography helper to request an invitation.
    content = (compatibility.load(compatibility.plain(root/'compatibility.json')) if mode in ('solo','lan')
               else compatibility.verify(root))
    expected = compatibility.network_id(content)
    state = compatibility.plain(root / 'LocalData'); state.mkdir(mode=0o700, exist_ok=True)
    url = game_directory.configured(state)
    if mode not in ('solo','lan','private-host','private-join','public-host','public-join'):
        raise ValueError('Unknown Linux play mode')
    network = None
    if mode.startswith(('private-', 'public-')):
        if consent is not True: raise ValueError('internet_consent_required')
        if mode == 'public-join':
            if url is None or game_id is None: raise ValueError('directory_join_unavailable')
            invitation = game_directory.join(url, game_id, {expected}, root=root,
                                             cancel=cancel_check)
        if mode.endswith('-join') and not invitation: raise ValueError('An invitation is required')
        network = dict(invitation=invitation if mode.endswith('-join') else None,
                       public_lobby=mode == 'public-host', internet_consent=1)
    request = None if mode == 'solo' else dict(player_name=player_name)
    room = [network.get('invitation') if network else None]
    bridges = []

    def invited(value):
        room[0] = value
        if invitation_ready: invitation_ready(value)

    def monitor(game):
        kind, code = configuration(request, network, state)
        invitation_bridge = directory_bridge = None
        try:
            invitation_bridge = InvitationBridge(root, kind,
                lambda **fields: notify('invitation_requested', **fields) if notify else None,
                public_code=code).start()
            directory_bridge = DirectoryBridge(root, url, {expected}).start() if url else None
        except (OSError,ValueError,RuntimeError):
            if invitation_bridge: invitation_bridge.close()
            invitation_bridge = None
            if directory_bridge: directory_bridge.close()
            directory_bridge = None
            if notify: notify('directory_unavailable')
        bridges.append(directory_bridge)
        if notify: notify('game_started', pid=game.pid)
        class Observer:
            def close(self):
                try:
                    if invitation_bridge: invitation_bridge.close()
                finally:
                    if directory_bridge: directory_bridge.close()
                if notify: notify('game_finished', exit_code=game.poll())
        return Observer()

    def public_observer(interface, coordinator, identity):
        if mode != 'public-host': return None
        reporter = PublicReporter(state,interface,coordinator,identity,room_invitation=room[0],
                                  version=version,helper_root=root)
        try:
            reporter.poll()
            return reporter.start()
        except BaseException:
            reporter.close(); raise

    if mode == 'solo':
        code = linux_runtime.run_local(root, monitor=monitor)
    else:
        args = SimpleNamespace(bundle=root, check_only=False, saved_graphics=True, player_name=player_name,
            game_monitor=monitor, cancel_check=cancel_check, public_observer=public_observer,
            switch_requested=lambda: any(bridge and bridge.pending for bridge in bridges))
        if network:
            import launch_internet
            vars(args).update(network)
            code = launch_internet.run(args,invitation_ready=invited)
        else:
            code = launch_auto_lan.run(args)
    pending = next((bridge.pending for bridge in bridges if bridge and bridge.pending),None)
    if pending and notify: notify('directory_switch',game_id=pending)
    return code
