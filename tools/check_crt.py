from math import isclose, isfinite, pow, sin, sqrt

REFERENCE_HEIGHT = 1080
SCANLINE_FREQUENCY = 1.2
MASK_PERIOD = 3.75
MASK_RAMP = 2.5


def curve(x: float, y: float) -> tuple[float, float]:
    x = (x - 0.5) * 2.0 * 1.1
    y = (y - 0.5) * 2.0 * 1.1
    x *= 1.0 + pow(abs(y) / 5.0, 2.0)
    y *= 1.0 + pow(abs(x) / 4.0, 2.0)
    return (x / 2.0 + 0.5) * 0.92 + 0.04, (y / 2.0 + 0.5) * 0.92 + 0.04


def warp(x: float, y: float) -> tuple[float, float]:
    curved_x, curved_y = curve(x, y)
    return x + (curved_x - x) * 0.5, y + (curved_y - y) * 0.5


def filmic(value: float) -> float:
    value = max(0.0, value - 0.004)
    output = (value * (6.2 * value + 0.5)) / (value * (6.2 * value + 1.7) + 0.06)
    return min(max(output, 0.0), 1.0)


def vignette(x: float, y: float) -> float:
    value = 0.1 + 16.0 * x * y * (1.0 - x) * (1.0 - y)
    return 1.3 * sqrt(value)


def display_scale(height: int) -> float:
    return height / REFERENCE_HEIGHT


def reference_pixel(coordinate: float, resolution: int, height: int) -> float:
    return coordinate * resolution / display_scale(height)


def scanline(y: float, height: int) -> float:
    value = min(
        max(
            0.35 + 0.18 * sin(reference_pixel(y, height, height) * SCANLINE_FREQUENCY),
            0.0,
        ),
        1.0,
    )
    return pow(value, 0.9)


def shadow_mask(x: float, height: int) -> float:
    phase = min(max(((x / display_scale(height)) % MASK_PERIOD) / MASK_RAMP, 0.0), 1.0)
    return 1.0 - 0.23 * phase


def edge_mask(x: float, y: float) -> float:
    distance = min(x, y, 1.0 - x, 1.0 - y)
    value = min(max(distance / 0.01, 0.0), 1.0)
    return value * value * (3.0 - 2.0 * value)


def main() -> None:
    assert isclose(SCANLINE_FREQUENCY, 1.5 / 1.25)
    assert isclose(MASK_PERIOD, 3.0 * 1.25)
    assert isclose(MASK_RAMP, 2.0 * 1.25)

    assert warp(0.5, 0.5) == (0.5, 0.5)
    for x, y in ((0.1, 0.2), (0.25, 0.75), (0.4, 0.9)):
        curved_x, curved_y = warp(x, y)
        mirrored_x, _ = warp(1.0 - x, y)
        _, mirrored_y = warp(x, 1.0 - y)
        assert isclose(curved_x, 1.0 - mirrored_x)
        assert isclose(curved_y, 1.0 - mirrored_y)

    for value in (-1.0, 0.0, 0.25, 1.0, 10.0, 1e6):
        output = filmic(value)
        assert isfinite(output)
        assert 0.0 <= output <= 1.0

    for x in (0.0, 0.25, 0.5, 0.75, 1.0):
        for y in (0.0, 0.25, 0.5, 0.75, 1.0):
            value = vignette(x, y)
            assert isfinite(value)
            assert 0.4 <= value <= 1.4

    for y in (0.0, 0.1, 0.5, 0.9, 1.0):
        value_1080 = scanline(y, 1080)
        value_2160 = scanline(y, 2160)
        assert isfinite(value_1080)
        assert 0.0 <= value_1080 <= 1.0
        assert isclose(value_1080, value_2160)

    for x in (0.0, 0.5, 1.0, 2.0, 2.5, 100.5):
        value_1080 = shadow_mask(x, 1080)
        value_1440 = shadow_mask(x * display_scale(1440), 1440)
        value_2160 = shadow_mask(x * display_scale(2160), 2160)
        assert isclose(value_1080, value_1440)
        assert isclose(value_1080, value_2160)

    assert edge_mask(-0.01, 0.5) == 0.0
    assert edge_mask(0.0, 0.5) == 0.0
    assert isclose(edge_mask(0.005, 0.5), 0.5)
    assert edge_mask(0.01, 0.5) == 1.0
    assert edge_mask(0.5, 0.5) == 1.0

    print("CRT self-check passed.")


if __name__ == "__main__":
    main()
