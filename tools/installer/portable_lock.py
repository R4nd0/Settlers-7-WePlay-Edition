"""Nonblocking, process-scoped copy locks on Windows and Linux."""
import os

if os.name == 'nt':
    from msvcrt import locking, LK_NBLCK, LK_UNLCK
else:
    import fcntl
    LK_NBLCK, LK_UNLCK = 2, 0

    def locking(descriptor, mode, length):
        if length != 1 or mode not in (LK_NBLCK, LK_UNLCK):
            raise ValueError('Invalid copy lock')
        fcntl.flock(descriptor, fcntl.LOCK_UN if mode == LK_UNLCK else fcntl.LOCK_EX | fcntl.LOCK_NB)
