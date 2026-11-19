"""Pump owned mixed-session services until explicit close or owned game exit."""
import threading


class SessionPump:
    def __init__(self, *, broker=None, gold=None, history=None, gateway=None,
                 services=(), game_limit=1):
        if (broker is None and gateway is None) or (gold is not None and broker is None):
            raise ValueError('crossplay_configuration_invalid')
        if type(game_limit) is not int or not 1 <= game_limit <= 8:
            raise ValueError('crossplay_configuration_invalid')
        self.broker, self.gold, self.gateway = broker, gold, gateway
        self.history=history
        self.services = tuple(services)
        self.game_limit = game_limit
        self.stop = threading.Event(); self.lock = threading.Lock()
        self.thread = None
        self.games = []
        self.failed = self.closed = False

    @property
    def game(self):
        with self.lock:
            return self.games[0] if self.games else None

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
            if (self.closed or self.stop.is_set() or len(self.games) >= self.game_limit
                    or any(game is current for current in self.games)
                    or not callable(getattr(game, 'poll', None))):
                raise ValueError('crossplay_configuration_invalid')
            self.games.append(game)
        return self

    def _release(self):
        for service in (self.gateway, self.broker, self.gold, self.history, *reversed(self.services)):
            if service is not None:
                try: service.close()
                except Exception: self.failed = True
        self.closed = True

    def _run(self):
        try:
            while not self.stop.is_set():
                with self.lock: games = tuple(self.games)
                if games and all(game.poll() is not None for game in games): break
                if self.broker is not None: self.broker.pump(0)
                if self.gold is not None: self.gold.pump(0)
                if self.history is not None:self.history.pump(0)
                for service in self.services:
                    service.pump(0)
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
