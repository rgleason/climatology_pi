"""Cross-process locks for long-running source checkpoints."""

from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
from typing import Iterator


@contextmanager
def checkpoint_lock(checkpoint: Path) -> Iterator[None]:
    """Fail fast when another process owns the same checkpoint.

    The lock file is intentionally retained: the operating-system lock, not
    file existence, represents ownership. This means a killed process cannot
    leave a stale lock which prevents a safe checkpoint resume.
    """
    path = checkpoint.with_suffix(checkpoint.suffix + ".lock")
    path.parent.mkdir(parents=True, exist_ok=True)
    stream = path.open("a+b")
    locked = False
    try:
        try:
            if os.name == "nt":  # pragma: no cover - exercised on Windows CI
                import msvcrt

                stream.seek(0, os.SEEK_END)
                if stream.tell() == 0:
                    stream.write(b"\0")
                    stream.flush()
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except OSError as exc:
            raise RuntimeError(
                f"another source builder already owns checkpoint {checkpoint}"
            ) from exc
        yield
    finally:
        if locked:
            if os.name == "nt":  # pragma: no cover - exercised on Windows CI
                import msvcrt

                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        stream.close()
