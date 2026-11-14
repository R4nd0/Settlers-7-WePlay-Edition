"""Debugger backend for fingerprinted portable Gold/History package copies."""
import ctypes as C
import json
import os
from pathlib import Path
import queue
import subprocess

import debugger
import launch_local
import memory_guard
import platform_standalone as platform


def run(args, helpers):
    root, output = args.bundle.resolve(), args.output.resolve()
    snapshot, backup, digest = (helpers[n] for n in ('snapshot','backup','digest'))
    value, exe, adapter_file = platform.load(root)
    marker_test=getattr(args,'self_test_gold_markers',False)
    marker_values=getattr(args,'gold_marker',[])
    capture=bool(marker_test or marker_values)
    if capture and value['edition']!='gold':raise ValueError('Gold marker capture requires Gold')
    if args.gold_adapter is not None and args.gold_adapter.resolve()!=adapter_file:
        raise ValueError('Portable copies require their packaged native adapter')
    if value['edition'] == 'history':launch_local.verify_bundle(root)
    import isolated_process
    if any(p.exe.lower() == 'settlers7r.exe' for p in isolated_process.Windows().processes()):
        raise RuntimeError('Close other S7 clients before the isolated debugger test')
    # Importing the native Frida extension can be slow on a cold host. Do that
    # before the game enters its bounded pre-OEP release wait, rather than
    # spending that wait on controller setup inside the attachment callback.
    import frida
    frida.get_local_device()
    memory_guard.require_host_headroom()
    documents = C.create_unicode_buffer(32768)
    if C.windll.shell32.SHGetFolderPathW(None,5,None,0,documents) != 0:
        raise RuntimeError('Cannot resolve Windows Documents')
    shared = [Path(documents.value)/n for n in ('Settlers7','THE SETTLERS 7 - History Edition','WePlay/Gold')]
    before = {str(p):snapshot(p) for p in shared}
    output.mkdir()
    for i,p in enumerate(shared):backup(p,output/'before'/str(i),before[str(p)])
    private = root/'LocalData/ProfileRoot'
    backup(private,output/'before/Portable',snapshot(private))
    immutable = {str(p.relative_to(root)):digest(p) for p in (exe,adapter_file)}
    source = Path(__file__).parent
    sources=['debugger.py','agent.js','stepping.js','lua_errors.js','fixture.lua','profiles.py','standalone_launch.py','launch.py']
    if capture:sources+=['gold_capture.py','gold_capture_agent.js','gold_capture_policy.js','gold_marker_fixture.lua','gold_marker_test.py']
    (output/'source-checkpoint.private.json').write_text(json.dumps({p.name:digest(p) for p in
        (source/n for n in sources)},indent=2))
    runtime = root/'LocalData/s7-local-runtime.ini'
    previous = runtime.read_bytes() if runtime.exists() else None
    service = game = sentinel = adapter = archive_fixture = None
    failure = None
    try:
        if marker_test:
            from gold_marker_test import ArchiveFixture
            archive_fixture=ArchiveFixture(root,output)
            archive_fixture.install()
        if value['edition'] == 'history':
            dotnet = args.dotnet or root/'Runtime/dotnet/dotnet.exe'
            server = args.service or root/'Server/S7Patcher.dll'
            with (output/'service.private.jsonl').open('xb') as log, (output/'service-errors.private.txt').open('xb') as err:
                service = subprocess.Popen([str(dotnet.resolve()),str(server.resolve()),'--local-config-server',
                    '--local-keepalive-probe','--http-port','0','--udp-port','0','--seconds','1800'],
                    stdout=log,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
            config,port = launch_local.service_ready(service,output/'service.private.jsonl')
            launch_local.write_configuration(root/'LocalData',config,port,True,display_name='LuaDebuggerLab')
        env = dict(os.environ,DXVK_LOG_PATH=str(output),DXVK_STATE_CACHE_PATH=str(root/'LocalData'))
        points = [dict(source=p.rsplit(':',1)[0],line=int(p.rsplit(':',1)[1])) for p in args.breakpoint]
        def attach(owned):
            nonlocal adapter
            if capture:
                import gold_capture
                adapter=gold_capture.GoldCaptureAdapter(exe,owned.pid,output/'debugger.private.jsonl',
                    allowed=gold_capture.TEST_MARKERS if marker_test else marker_values,selftest=marker_test,
                    selected=('log','console') if getattr(args,'gold_channel','both')=='both' else (args.gold_channel,))
            else:
                adapter = debugger.Adapter(exe,owned.pid,output/'debugger.private.jsonl',selftest=args.self_test,
                                           pause_ms=args.pause_ms,breakpoints=points,trace_errors_only=args.trace_errors_only)
        arguments=('-vSynch',) if value['edition']=='gold' else ('-vSynch','-noMapInMainMenu')
        game = platform.launch(root,environment=env,arguments=arguments,on_initialized=attach)
        (output/'owned.private.json').write_text(json.dumps(dict(pid=game.pid,parent=os.getpid(),path=str(exe),
            sha256=digest(exe),kernel_isolation=game.kernel_isolation),indent=2))
        sentinel = memory_guard.Sentinel(game,output/'memory.private.json',memory_guard.Probe(game,exe));sentinel.start()
        print(json.dumps(dict(event='debugger-launched',pid=game.pid,portable=True)),flush=True)
        if args.self_test or marker_test:
            result = adapter.selftest()
            (output/'test-result.private.json').write_text(json.dumps(result,indent=2))
            print(json.dumps(dict(event='live-self-test-passed',result=result)),flush=True)
        else:
            offset = 0
            while game.poll() is None:
                try:print(json.dumps(adapter.events.get(timeout=.1)),flush=True)
                except queue.Empty:pass
                if args.commands is not None and args.commands.exists():
                    if args.commands.stat().st_size>1024*1024:raise ValueError('Command file exceeds 1 MiB')
                    with args.commands.open(encoding='utf-8') as stream:
                        stream.seek(offset)
                        while True:
                            line = stream.readline()
                            if not line.endswith('\n'):break
                            adapter.command(**json.loads(line));offset=stream.tell()
    except BaseException as error:
        failure = error
        (output/'failure.private.json').write_text(json.dumps(dict(type=type(error).__name__,detail=str(error))))
        if adapter and not adapter.closed:
            try:adapter.close_after_restored(timeout=10)
            except Exception:pass
    finally:
        if game:game.wait(close=False)
        if sentinel:sentinel.close()
        if adapter and not adapter.closed:adapter.close_after_restored(timeout=5)
        if game:game.close()
        archive_restored=archive_fixture.restore() if archive_fixture else None
        if service and service.poll() is None:service.terminate();service.wait(timeout=15)
        if previous is None:runtime.unlink(missing_ok=True)
        else:runtime.write_bytes(previous)
        unchanged = before == {str(p):snapshot(p) for p in shared}
        finish = dict(exit_code=game.returncode if game else None,shared_profiles_unchanged=unchanged,
                      game_files_unchanged=all(digest(root/n)==h for n,h in immutable.items()),
                      diagnostic_archive_restored=archive_restored)
        (output/'restoration.private.json').write_text(json.dumps(finish,indent=2))
        print(json.dumps(finish),flush=True)
        if not unchanged:raise RuntimeError('Shared profiles changed; newer saves retained without replacement')
    if failure:raise failure
    return game.returncode
