from __future__ import annotations

from math import floor, isfinite

MIN_SCALE = 0.25
MAX_SCALE = 3.0


def normalize_scale(value: float) -> float:
    if not isfinite(value):
        return 1.0
    return min(max(value, MIN_SCALE), 1.0)


def effective_scale(render_scale: float, ssaa_factor: float) -> float:
    return normalize_scale(render_scale) * ssaa_factor


def scaled_dimension(output: int, scale: float) -> int:
    return max(floor(output * scale + 0.5), 1)


def needs_downscale(
    output_w: int, output_h: int, internal_w: int, internal_h: int
) -> bool:
    return internal_w > output_w or internal_h > output_h


def main() -> None:
    assert effective_scale(0.5, 2.0) == 1.0
    assert effective_scale(1.0, 3.0) == 3.0
    assert effective_scale(0.25, 1.5) == 0.375

    assert scaled_dimension(1920, 1.0) == 1920
    assert scaled_dimension(1920, 1.5) == 2880
    assert scaled_dimension(1920, 2.0) == 3840
    assert scaled_dimension(1920, 3.0) == 5760
    assert scaled_dimension(960, 2.0) == 1920
    assert scaled_dimension(1920, 0.375) == 720

    assert needs_downscale(1920, 1080, 1920, 1080) is False
    assert needs_downscale(1920, 1080, 3840, 2160) is True
    assert needs_downscale(1920, 1080, 960, 540) is False

    print("SSAA self-check passed.")


if __name__ == "__main__":
    main()
