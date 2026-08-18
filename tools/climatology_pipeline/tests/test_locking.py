from __future__ import annotations

from pathlib import Path

import pytest

from climatology_pipeline.locking import checkpoint_lock


def test_checkpoint_lock_rejects_concurrent_owner_and_survives_release(
    tmp_path: Path,
) -> None:
    checkpoint = tmp_path / "source.npz"
    with checkpoint_lock(checkpoint):
        with pytest.raises(RuntimeError, match="already owns checkpoint"):
            with checkpoint_lock(checkpoint):
                pass

    # The persistent lock-file inode is reusable and cannot become stale when
    # the owning process exits or is killed.
    with checkpoint_lock(checkpoint):
        assert checkpoint.with_suffix(".npz.lock").is_file()
