"""Durian canopy detection on camera frames (OpenCV).

Finds green foliage with an HSV colour mask so the operator can see when the spray
head is pointed at a tree. It only annotates the frame and reports a ratio; it never
switches the pump on by itself.

Pipeline per frame:
    GaussianBlur -> BGR2HSV -> inRange(green) -> morphology OPEN + CLOSE
    -> green pixel ratio -> largest contour -> bounding box + label overlay
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

# OpenCV hue runs 0-179. Durian leaves sit roughly between yellow-green and
# blue-green; saturation/value floors drop grey sky, soil and deep shadow.
GREEN_LOWER = (35, 60, 40)
GREEN_UPPER = (85, 255, 255)
BLUR_KERNEL = (5, 5)
MORPH_KERNEL_SIZE = 5
# Blobs smaller than this share of the frame are treated as noise, not a tree.
MIN_CONTOUR_RATIO = 0.01


@dataclass(frozen=True, slots=True)
class CanopyResult:
    ratio: float                                  # green pixels / all pixels, 0.0-1.0
    detected: bool                                # ratio >= the "ready to spray" threshold
    box: tuple[int, int, int, int] | None         # x, y, w, h of the largest green blob


class CanopyDetector:
    def __init__(self, ready_ratio: float) -> None:
        import cv2

        self._cv2 = cv2
        self._ready_ratio = ready_ratio
        self._kernel = cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE)
        )
        self._lower: Any = None
        self._upper: Any = None

    def detect(self, frame: Any) -> CanopyResult:
        cv2 = self._cv2
        if self._lower is None:
            import numpy as np

            self._lower = np.array(GREEN_LOWER, dtype=np.uint8)
            self._upper = np.array(GREEN_UPPER, dtype=np.uint8)

        blurred = cv2.GaussianBlur(frame, BLUR_KERNEL, 0)
        hsv = cv2.cvtColor(blurred, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, self._lower, self._upper)
        # OPEN removes speckles of green noise, CLOSE fills gaps between leaves.
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, self._kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, self._kernel)

        ratio = cv2.countNonZero(mask) / float(mask.size)

        box = None
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if contours:
            largest = max(contours, key=cv2.contourArea)
            if cv2.contourArea(largest) >= MIN_CONTOUR_RATIO * mask.size:
                x, y, w, h = cv2.boundingRect(largest)
                box = (int(x), int(y), int(w), int(h))

        return CanopyResult(ratio=ratio, detected=ratio >= self._ready_ratio, box=box)

    def annotate(self, frame: Any, result: CanopyResult) -> None:
        """Draw the result onto the frame in place (BGR colours)."""
        cv2 = self._cv2
        color = (0, 220, 0) if result.detected else (0, 200, 255)
        if result.box is not None:
            x, y, w, h = result.box
            cv2.rectangle(frame, (x, y), (x + w, y + h), color, 2)
        label = f"CANOPY {result.ratio:.0%}" + ("  READY" if result.detected else "")
        cv2.rectangle(frame, (0, 0), (8 + 11 * len(label), 26), (0, 0, 0), -1)
        cv2.putText(frame, label, (6, 19), cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 1, cv2.LINE_AA)
