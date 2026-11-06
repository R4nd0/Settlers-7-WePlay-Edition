"""Native Linux installer/launcher, CLI and Tk desktop entry point."""
import argparse
import http.client
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
from contextlib import nullcontext
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
if (TOOLS / 'installer').is_dir():
    sys.path[:0] = [str(HERE), str(TOOLS / 'installer'), str(TOOLS / 'offline-ui'), str(TOOLS / 'lan')]
import compatibility
import linux_runtime


def prepare(package, source, destination, *, emit=print, progress=None):
    import install
    linux_runtime.require_linux_filesystem(Path(destination).absolute().parent)
    manifest = install.package_manifest(package)
    if not manifest.get('linux_native', False):
        raise ValueError('Select a native Linux package')
    source = compatibility.plain(source)
    managed = (source / 'compatibility.json').is_file()
    native_update = managed and (source / 'Launcher/linux_runtime.py').is_file()
    from launch_auto_lan import session_lock
    with session_lock(source) if native_update else nullcontext():
        result = install.prepare(package, source, destination, emit=emit, progress=progress,
                                 managed=managed, publish_starters=False)
        incomplete = result / 'INSTALLATION-INCOMPLETE.txt'
        incomplete.write_text('Linux setup is still completing.\n',encoding='utf-8')
        if native_update:
            import linux_profiles
            linux_profiles.migrate(source,result)
    # copyfile verifies bytes; executable permissions are a separate Linux step.
    for name in ('Server/S7Patcher', 'Server/WePlay.LinuxHelper'):
        path = compatibility.plain(result / name)
        if path.is_file(): path.chmod(path.stat().st_mode | 0o111)
    for name in ('Runtime/easytier/easytier-core', 'Runtime/easytier/easytier-cli'):
        path = result / name
        if path.is_file():
            path.chmod(path.stat().st_mode | 0o111)
    starter = result / 'WePlay.sh'
    with starter.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write('#!/bin/sh\nset -eu\nROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                     'exec python3 -B "$ROOT/Launcher/linux_launcher.py" --bundle "$ROOT" "$@"\n')
    starter.chmod(0o755)
    incomplete.unlink()
    print('Prepared independent Linux game copy. Original installation preserved.', flush=True)
    return result


def play(root, mode='solo', *, check_only=False, player_name='LocalPlayer', **options):
    if not check_only and os.geteuid()==0:
        raise ValueError('Start WePlay as your normal user, without sudo')
    if check_only and mode == 'solo':
        return linux_runtime.run_local(root, check_only=check_only)
    if check_only:
        import launch_auto_lan
        return launch_auto_lan.run(SimpleNamespace(bundle=Path(root), check_only=True))
    import linux_network
    return linux_network.run(root,mode,player_name=player_name,**options)


def dxvk_source(root, package=None, explicit=None):
    if explicit is not None: return compatibility.plain(explicit)
    candidates = [Path(root) / 'Runtime/Graphics/d3d9.dll']
    if package is not None: candidates.append(Path(package) / 'Payload/Graphics/d3d9.dll')
    for path in candidates:
        if compatibility.plain(path).is_file(): return path
    raise ValueError('This package has no bundled DXVK renderer. Select its pinned x32 DLL with --dll.')


def gui(package=None, bundle=None):
    import tkinter as tk
    import player_settings
    from tkinter import filedialog, messagebox, simpledialog, ttk
    window = tk.Tk(); window.title('Settlers 7 · WePlay · Linux')
    window.geometry('820x650'); window.minsize(750, 580)
    events = queue.Queue(); active = [False]; current = [bundle]
    cancelled = threading.Event(); private_invitation = [None]; pending_game = [None]
    frame = ttk.Frame(window, padding=24); frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='SETTLERS 7 · WEPLAY', font=('Sans', 23, 'bold')).pack(anchor='w')
    ttk.Label(frame, text='Linux · Wine / Proton · WePlay Community').pack(anchor='w', pady=(4, 18))
    status = tk.StringVar(value=str(bundle) if bundle else 'Select your own History Edition to prepare a separate copy.')
    ttk.Label(frame, textvariable=status, wraplength=690).pack(anchor='w', pady=8)
    controls = ttk.Frame(frame); controls.pack(fill='x', pady=10)
    network_controls = ttk.Frame(frame); network_controls.pack(fill='x', pady=4)
    settings_controls = ttk.Frame(frame); settings_controls.pack(fill='x', pady=4)
    buttons = []
    output = tk.Text(frame, height=13, wrap='word', state='disabled'); output.pack(fill='both', expand=True, pady=10)
    progress = ttk.Progressbar(frame, mode='indeterminate'); progress.pack(fill='x')
    name = tk.StringVar(value=player_settings.load(bundle) if bundle else player_settings.DEFAULT_NAME)
    player = ttk.Frame(frame); player.pack(fill='x', pady=8)
    ttk.Label(player, text='Player / Spieler: ').pack(side='left')
    ttk.Entry(player, textvariable=name, width=26).pack(side='left')

    def log(text):
        output.configure(state='normal'); output.insert('end', str(text) + '\n')
        if int(output.index('end-1c').split('.')[0]) > 400:
            output.delete('1.0', '100.0')
        output.see('end'); output.configure(state='disabled')

    def operation(function, result_kind='done'):
        if active[0]: return
        cancelled.clear()
        active[0] = True; progress.start()
        for button in buttons: button.configure(state='disabled')
        def run():
            try: events.put((result_kind, function()))
            except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException) as error:
                events.put(('error', str(error)))
        threading.Thread(target=run, daemon=False).start()

    def install_game():
        source = filedialog.askdirectory(title='Your original History Edition or existing WePlay copy')
        if not source: return
        parent = filedialog.askdirectory(title='Parent folder for the new independent copy')
        if not parent: return
        import datetime
        destination = Path(parent) / ('WePlay-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
        def work():
            current[0] = prepare(package, source, destination, emit=lambda text: events.put(('log', text)))
            events.put(('player', player_settings.load(current[0])))
            return 'Prepared: ' + str(current[0])
        operation(work)

    def launch(mode, check=False, game_id=None):
        if current[0] is None:
            messagebox.showinfo('WePlay', 'Prepare or select a game copy first.'); return
        selected_name = name.get()
        consent = False; invitation = None
        if mode.startswith(('private-', 'public-')):
            consent = messagebox.askyesno('EasyTier', 'Connect this PC to the selected virtual game network?\n'
                'Players can see connection addresses. Only game/lobby traffic is permitted by the managed tunnel.\n'
                'Linux may ask to authorize its network helper; the game remains a normal user process.')
            if not consent: return
            if mode == 'private-join':
                invitation = simpledialog.askstring('WePlay', 'Private invitation / Private Einladung:', show='*')
                if not invitation: return
        def checkpoint():
            if cancelled.is_set(): raise RuntimeError('Room closed / Raum geschlossen')
        def notify(kind, **fields):
            if kind == 'directory_switch': events.put(('switch', fields['game_id']))
            else: events.put(('log', kind))
        def play_selected():
            code = play(current[0], mode, check_only=check, player_name=selected_name,
                consent=consent,invitation=invitation,game_id=game_id,
                invitation_ready=lambda value:events.put(('invitation',value)),
                notify=notify,cancel_check=checkpoint)
            if code: raise RuntimeError('The game or service stopped with an error. Check its local diagnostic log.')
            return 'Verification completed.' if check else 'Game closed.'
        operation(play_selected)

    def choose():
        selected = filedialog.askdirectory(title='Prepared WePlay game copy')
        if selected:
            current[0] = Path(selected); status.set(str(current[0]))

    def runner_settings():
        if current[0] is None: return
        kind = simpledialog.askstring('Wine / Proton', 'Runner: wine or proton', initialvalue='wine')
        if kind is None: return
        kind = kind.strip().lower()
        if kind not in ('wine', 'proton'):
            messagebox.showerror('WePlay', 'Select wine or proton.'); return
        program = filedialog.askopenfilename(title='Select the installed ' + kind + ' executable')
        if not program: return
        steam = filedialog.askdirectory(title='Linux Steam client directory') if kind == 'proton' else None
        if kind == 'proton' and not steam: return
        selected = current[0]
        operation(lambda: (linux_runtime.configure(selected, runner=kind, executable=program, steam=steam),
                           'Runner saved.')[1])

    def renderer_settings():
        if current[0] is None: return
        dxvk = messagebox.askyesnocancel('Renderer',
            'Use the bundled DXVK renderer?\nYes: DXVK (Vulkan driver required)\nNo: WineD3D')
        if dxvk is None: return
        selected = current[0]
        def save():
            import graphics_profile
            if dxvk: graphics_profile.install(selected, dxvk_source(selected, package))
            else: graphics_profile.restore(selected)
            return 'Renderer saved.'
        operation(save)

    for text, callback in [('Wine / Proton', runner_settings), ('Renderer', renderer_settings)]:
        button = ttk.Button(settings_controls, text=text, command=callback)
        button.pack(side='left', padx=(0, 7)); buttons.append(button)

    for text, callback in [('Solo', lambda: launch('solo')), ('LAN', lambda: launch('lan')),
                           ('Verify / Prüfen', lambda: launch('solo', True)), ('Open copy / Kopie öffnen', choose)]:
        button = ttk.Button(controls, text=text, command=callback); button.pack(side='left', padx=(0, 7)); buttons.append(button)
    def public_games():
        if current[0] is None: return
        import linux_network
        operation(lambda:linux_network.games(current[0]),'games')
    for text,callback in [('Private host',lambda:launch('private-host')),('Private join',lambda:launch('private-join')),
                          ('Public host',lambda:launch('public-host')),('Public games',public_games)]:
        button=ttk.Button(network_controls,text=text,command=callback)
        button.pack(side='left',padx=(0,7));buttons.append(button)
    def copy_invitation():
        if private_invitation[0]:
            window.clipboard_clear(); window.clipboard_append(private_invitation[0])
    ttk.Button(player,text='Copy invitation',command=copy_invitation).pack(side='right',padx=6)
    ttk.Button(player,text='Close room',command=cancelled.set).pack(side='right',padx=6)
    if package:
        button = ttk.Button(player, text='Install / Update', command=install_game); button.pack(side='right'); buttons.append(button)

    def poll():
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == 'player':
                    name.set(value)
                    continue
                if kind == 'invitation':
                    private_invitation[0]=value;log('Private invitation ready. Use Copy invitation to share it.')
                    continue
                if kind == 'switch':
                    pending_game[0] = value
                    continue
                if kind == 'games':
                    dialog=tk.Toplevel(window);dialog.title('Public games / Öffentliche Partien')
                    tree=ttk.Treeview(dialog,columns=('title','players','compatible'),show='headings',height=10)
                    for key,label in [('title','Game / Partie'),('players','Players'),('compatible','Version')]:tree.heading(key,text=label)
                    for row in value:
                        tree.insert('', 'end',iid=row['id'],values=(row['title'],str(row['players'])+'/'+str(row['capacity']),
                            'OK' if row['compatible'] else 'Different build'))
                    tree.pack(fill='both',expand=True,padx=12,pady=12)
                    def join():
                        selected=tree.selection()
                        if selected:
                            game=selected[0];dialog.destroy();launch('public-join',game_id=game)
                    ttk.Button(dialog,text='Join / Beitreten',command=join).pack(pady=(0,12))
                else: log(value)
                if kind in ('done', 'error', 'games'):
                    active[0] = False; progress.stop()
                    for button in buttons: button.configure(state='normal')
                    if current[0]: status.set(str(current[0]))
                    if kind == 'error': messagebox.showerror('WePlay', value)
                    if kind == 'done' and pending_game[0] is not None:
                        game, pending_game[0] = pending_game[0], None
                        window.after_idle(lambda selected=game: launch('public-join', game_id=selected))
        except queue.Empty: pass
        window.after(100, poll)

    def close():
        if active[0]:
            messagebox.showinfo('WePlay', 'Close the game or wait for preparation to finish first.'); return
        window.destroy()
    window.protocol('WM_DELETE_WINDOW', close)
    poll(); window.mainloop()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path)
    parser.add_argument('--bundle', type=Path)
    commands = parser.add_subparsers(dest='action')
    installation = commands.add_parser('install'); installation.add_argument('--source', type=Path, required=True)
    installation.add_argument('--destination', type=Path, required=True)
    run = commands.add_parser('play'); run.add_argument('--mode', choices=('solo','lan','private-host','private-join','public-host','public-join'), default='solo')
    run.add_argument('--check-only', action='store_true'); run.add_argument('--player-name', default='LocalPlayer')
    run.add_argument('--allow-internet',action='store_true');run.add_argument('--game-id')
    run.add_argument('--invitation-file',type=Path,
                     help='New private file for a hosted room invitation; never written to terminal output')
    commands.add_parser('directory')
    commands.add_parser('verify')
    configuration = commands.add_parser('runner')
    configuration.add_argument('--kind', choices=('wine', 'proton'), default='wine')
    configuration.add_argument('--executable', required=True); configuration.add_argument('--steam', type=Path)
    graphics = commands.add_parser('graphics'); graphics.add_argument('renderer', choices=('wined3d', 'dxvk'))
    graphics.add_argument('--dll', type=Path)
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('Use the native Linux launcher on Linux')
    package = args.package or (TOOLS.parent if (TOOLS.parent / 'package.json').is_file() else None)
    bundle = args.bundle or (HERE.parent if HERE.name == 'Launcher' else None)
    try:
        if args.action == 'install':
            if package is None: raise ValueError('--package is required')
            prepare(package, args.source, args.destination); return 0
        if args.action is None: return gui(package, bundle)
        if bundle is None: raise ValueError('--bundle is required')
        if args.action == 'verify': return play(bundle, check_only=True)
        if args.action == 'play':
            invitation = None
            if args.mode == 'private-host' and args.invitation_file is None:
                raise ValueError('Private CLI hosting requires --invitation-file pointing to a new private file')
            def invitation_ready(value):
                if args.invitation_file is not None:
                    path = compatibility.plain(args.invitation_file.absolute())
                    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as stream:
                        stream.write(value + '\n')
                    print('Room invitation saved in the selected private file.', flush=True)
            def notify(kind, **fields):
                if kind == 'directory_switch':
                    print('Public game selected: ' + fields['game_id'] +
                          '. Use play --mode public-join --game-id ID --allow-internet to connect.', flush=True)
            if args.mode == 'private-join':
                from getpass import getpass
                invitation=getpass('WePlay invitation: ')
            return play(bundle,args.mode,check_only=args.check_only,player_name=args.player_name,
                        consent=args.allow_internet,invitation=invitation,game_id=args.game_id,
                        invitation_ready=invitation_ready,notify=notify)
        if args.action == 'directory':
            import linux_network
            print(json.dumps(linux_network.games(bundle),ensure_ascii=False,indent=2));return 0
        if args.action == 'runner':
            linux_runtime.configure(bundle, runner=args.kind, executable=args.executable, steam=args.steam); return 0
        if args.action == 'graphics':
            import graphics_profile
            if args.renderer == 'dxvk':
                graphics_profile.install(bundle, dxvk_source(bundle, package, args.dll))
            else: graphics_profile.restore(bundle)
            return 0
    except KeyboardInterrupt: return 130
    except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException) as error:
        print('WePlay: ' + str(error), file=sys.stderr); return 2


if __name__ == '__main__':
    raise SystemExit(main())
