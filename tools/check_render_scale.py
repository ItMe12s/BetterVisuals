from __future__ import annotations

from math import floor, isfinite


MIN_SCALE = 0.25
MAX_SCALE = 1.0


def normalize_scale(value: float) -> float:
    if not isfinite(value):
        return 1.0
    return min(max(value, MIN_SCALE), MAX_SCALE)


def scaled_dimension(output: int, scale: float) -> int:
    return max(floor(output * scale + 0.5), 1)


def source_index(output_index: int, output_size: int, internal_size: int) -> int:
    index = floor((output_index + 0.5) * internal_size / output_size)
    return min(max(index, 0), internal_size - 1)


def scaled_scissor(
    x: int,
    y: int,
    w: int,
    h: int,
    full_w: int,
    full_h: int,
    internal_w: int,
    internal_h: int,
) -> tuple[int, int, int, int]:
    scale_x = internal_w / full_w
    scale_y = internal_h / full_h
    return (int(x * scale_x), int(y * scale_y), int(w * scale_x), int(h * scale_y))


def main() -> None:
    assert normalize_scale(float("nan")) == 1.0
    assert normalize_scale(float("inf")) == 1.0
    assert normalize_scale(-1.0) == MIN_SCALE
    assert normalize_scale(2.0) == MAX_SCALE

    assert scaled_dimension(1920, 1.0) == 1920
    assert scaled_dimension(1920, 0.75) == 1440
    assert scaled_dimension(1920, 0.5) == 960
    assert scaled_dimension(1, MIN_SCALE) == 1
    assert scaled_dimension(1919, 0.5) == 960

    assert [source_index(index, 4, 2) for index in range(4)] == [0, 0, 1, 1]
    assert source_index(0, 1920, 960) == 0
    assert source_index(1919, 1920, 960) == 959

    assert scaled_scissor(0, 0, 1920, 1080, 1920, 1080, 960, 540) == (0, 0, 960, 540)
    assert scaled_scissor(100, 200, 300, 400, 1920, 1080, 960, 540) == (
        50,
        100,
        150,
        200,
    )
    assert scaled_scissor(0, 0, 1920, 1080, 1920, 1080, 1920, 1080) == (
        0,
        0,
        1920,
        1080,
    )
    assert scaled_scissor(1920, 1080, 1920, 1080, 1920, 1080, 960, 540) == (
        960,
        540,
        960,
        540,
    )

    print("Render scale self-check passed.")


if __name__ == "__main__":
    main()
