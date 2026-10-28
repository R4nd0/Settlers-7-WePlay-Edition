"""Ownership, desktop-change and lifecycle checks for borderless placement."""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools/installer'),
               str(Path(__file__).resolve().parents[1] / 'tools/offline-ui')]
import window_mode as mode
import launch_local


class WindowModeTests(unittest.TestCase):
    def setUp(self):
        self.screen = dict(device=r'\\.\DISPLAY2',x=-1920,y=0,width=1920,height=1080)
        self.api = Mock()
        self.controller = mode.Controller(self.api,self.screen)

    def test_only_one_owned_candidate_is_placed_and_ambiguity_is_rejected(self):
        self.api.candidates.return_value = []
        self.controller.tick([self.screen]); self.api.place.assert_not_called()
        self.api.candidates.return_value = [37]
        self.controller.tick([self.screen])
        self.api.place.assert_called_once_with(37,(-1920,0,1920,1080))
        self.api.place.reset_mock(); self.api.candidates.return_value = [37,38]
        with self.assertRaises(ValueError): self.controller.tick([self.screen])
        self.api.place.assert_not_called()

    def test_removed_or_changed_desktop_never_moves_window(self):
        for screens in ([],[dict(self.screen,x=0)],[dict(self.screen,width=1280)]):
            with self.assertRaises(ValueError): self.controller.tick(screens)
        self.api.candidates.assert_not_called(); self.api.place.assert_not_called()
        for changes in ({'x':True},{'x':-999999},{'width':7680},{'width':3840,'height':3840}):
            with self.assertRaises(ValueError): mode.bounds(dict(self.screen,**changes))

    def test_hwnd_ownership_loss_prevents_each_mutation(self):
        api = mode.Windows.__new__(mode.Windows)
        api.api = Mock(); api.get_style = Mock(return_value=mode.OVERLAPPED)
        api.set_style = Mock()
        def rect(window,pointer):
            value=C.cast(pointer,C.POINTER(W.RECT)).contents
            value.left=0; value.top=0; value.right=1280; value.bottom=720
            return True
        api.api.GetWindowRect.side_effect=rect
        api.owned=Mock(return_value=False)
        self.assertFalse(api.place(37,(-1920,0,1920,1080)))
        api.api.GetWindowRect.assert_not_called()
        api.owned=Mock(side_effect=[True,False])
        self.assertFalse(api.place(37,(-1920,0,1920,1080)))
        api.set_style.assert_not_called(); api.api.SetWindowPos.assert_not_called()

    def test_unchanged_borderless_window_does_not_repeat_style_or_position_writes(self):
        api = mode.Windows.__new__(mode.Windows)
        api.api = Mock(); api.get_style=Mock(return_value=0x10000000)
        api.set_style=Mock(); api.owned=Mock(return_value=True)
        def rect(window,pointer):
            value=C.cast(pointer,C.POINTER(W.RECT)).contents
            value.left=-1920; value.top=0; value.right=0; value.bottom=1080
            return True
        api.api.GetWindowRect.side_effect=rect
        self.assertFalse(api.place(37,(-1920,0,1920,1080)))
        api.set_style.assert_not_called(); api.api.SetWindowPos.assert_not_called()

    def test_worker_stops_on_error_and_restores_only_its_thread_dpi_context(self):
        self.api.alive.return_value=True
        self.api.api.SetThreadDpiAwarenessContext.return_value=29
        with patch.object(mode.display,'enumerate_displays',return_value=[]):
            self.controller.run()
        self.assertIsNotNone(self.controller.failure)
        self.assertEqual(self.api.api.SetThreadDpiAwarenessContext.call_args.args,(29,))
        self.api.place.assert_not_called()

    def test_launcher_closes_worker_on_wait_failure_and_skips_when_disabled(self):
        game=Mock(); game.wait.side_effect=RuntimeError('fixture')
        module=Mock(); controller=module.start_window_mode.return_value
        with patch.object(launch_local,'_graphics_module',return_value=module):
            with self.assertRaises(RuntimeError): launch_local.wait_for_graphics_game(Path('.'),game)
        module.start_window_mode.assert_called_once_with(Path('.'),game)
        controller.close.assert_called_once()
        game.wait.side_effect=None; game.wait.return_value=0
        with patch.object(launch_local,'_graphics_module') as load:
            self.assertEqual(launch_local.wait_for_graphics_game(None,game,enabled=False),0)
        load.assert_not_called()


if __name__ == '__main__': unittest.main()
