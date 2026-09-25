"""OpenCV canopy detector tests on synthetic frames."""

import pytest

cv2 = pytest.importorskip("cv2")
np = pytest.importorskip("numpy")

from apiapp.infrastructure.vision import CanopyDetector


def _frame(green_fraction: float) -> "np.ndarray":
    """Brown soil background with a green block covering the left share of the frame."""
    frame = np.zeros((240, 320, 3), dtype=np.uint8)
    frame[:] = (40, 70, 110)  # BGR brown
    width = int(320 * green_fraction)
    if width:
        frame[:, :width] = (40, 160, 50)  # BGR leaf green
    return frame


def test_detects_canopy_when_green_fills_the_frame() -> None:
    detector = CanopyDetector(ready_ratio=0.25)
    result = detector.detect(_frame(0.5))

    assert result.detected is True
    assert result.ratio == pytest.approx(0.5, abs=0.03)
    assert result.box is not None
    x, _, w, _ = result.box
    assert x == 0
    assert w == pytest.approx(160, abs=4)


def test_no_canopy_on_bare_soil() -> None:
    detector = CanopyDetector(ready_ratio=0.25)
    result = detector.detect(_frame(0.0))

    assert result.detected is False
    assert result.ratio == 0.0
    assert result.box is None


def test_small_green_patch_is_below_ready_threshold() -> None:
    detector = CanopyDetector(ready_ratio=0.25)
    result = detector.detect(_frame(0.1))

    assert result.detected is False
    assert result.ratio == pytest.approx(0.1, abs=0.03)


def test_annotate_draws_on_the_frame_in_place() -> None:
    detector = CanopyDetector(ready_ratio=0.25)
    frame = _frame(0.5)
    before = frame.copy()
    detector.annotate(frame, detector.detect(frame))

    assert not np.array_equal(before, frame)
    assert frame.shape == before.shape
