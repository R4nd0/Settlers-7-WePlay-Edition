"""Pump owned mixed-session services until explicit close or owned game exit."""
import threading


class SessionPump:
    def __init__(self, *, broker=None, gold=None, history=None, gateway=None):
        if (broker is None and gateway is None) or (gold is not None and broker is None):
            raise ValueError('crossplay_configuration_invalid')
        self.broker, self.gold, self.gateway = broker, gold, gateway
        self.history=history
        self.stop = threading.Event(); self.lock = threading.Lock()
        self.thread = self.game = None
        self.failed = self.closed = False

    def start(self):
        if self.thread is not None or self.closed:
            raise ValueError('crossplay_service_closed')
        self.thread = threading.Thread(target=self._run, name='WePlay mixed session', daemon=True)
        try: self.thread.start()
        except BaseException:
            self.thread = None; self._release(); raise
        return self

    def attach(self, game):
        with self.lock:
            if self.game is not None or self.closed or not callable(getattr(game, 'poll', None)):
                raise ValueError('crossplay_configuration_invalid')
            self.game = game
        return self

    def _release(self):
        for service in (self.gateway, self.broker, self.gold, self.history):
            if service is not None:
                try: service.close()
                except Exception: self.failed = True
        self.closed = True

    def _run(self):
        try:
            while not self.stop.is_set():
                with self.lock: game = self.game
                if game is not None and game.poll() is not None: break
                if self.broker is not None: self.broker.pump(0)
                if self.gold is not None: self.gold.pump(0)
                if self.history is not None:self.history.pump(0)
                if self.gateway is not None:
                    self.gateway.pump()
                    if self.gateway.closed: break
                self.stop.wait(.005)
        except Exception:
            # Never log exception payloads or terminate a healthy game to hide
            # service failure. Closing the authenticated channel lets its native
            # owner take the original disconnect path.
            self.failed = True
        finally:
            self._release()

    def check(self):
        if self.failed: raise RuntimeError('crossplay_service_failed')

    def close(self):
        self.stop.set()
        if self.thread is not None:
            self.thread.join(timeout=5)
            if self.thread.is_alive(): raise RuntimeError('crossplay_service_shutdown_failed')
        elif not self.closed: self._release()

    def __enter__(self): return self.start()

    def __exit__(self, *_args): self.close()
