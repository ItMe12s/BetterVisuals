from math import floor, isclose, isfinite

BAYER_4X4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)
REFERENCE_HEIGHT = 1080
BASE_CELL_SIZE = 4
CHANNEL_LEVELS = (8, 8, 4)


def bayer2(x: int, y: int) -> int:
    if y < 1:
        return 0 if x < 1 else 2
    return 3 if x < 1 else 1


def bayer4_value(x: int, y: int) -> int:
    x %= 4
    y %= 4
    return 4 * bayer2(x % 2, y % 2) + bayer2(x // 2, y // 2)


def bayer4(x: int, y: int) -> float:
    return (bayer4_value(x, y) + 0.5) / 16.0


def cell_size(height: int) -> int:
    return max(floor(BASE_CELL_SIZE * height / REFERENCE_HEIGHT + 0.5), 1)


def cell_for_pixel(x: float, y: float, height: int) -> tuple[int, int]:
    size = cell_size(height)
    return floor(x / size), floor(y / size)


def source_center(x: float, y: float, width: int, height: int) -> tuple[float, float]:
    size = cell_size(height)
    cell_x, cell_y = cell_for_pixel(x, y, height)
    center_x = (cell_x + 0.5) * size
    center_y = (cell_y + 0.5) * size
    return min(max(center_x, 0.5), width - 0.5), min(max(center_y, 0.5), height - 0.5)


def quantize(value: float, threshold: float, levels: int) -> float:
    scaled = min(max(value, 0.0), 1.0) * (levels - 1)
    level = floor(scaled) + (1 if scaled - floor(scaled) >= threshold else 0)
    return min(level, levels - 1) / (levels - 1)


def quantize_rgb(
    color: tuple[float, float, float], threshold: float
) -> tuple[float, float, float]:
    r, g, b = color
    rl, gl, bl = CHANNEL_LEVELS
    return (
        quantize(r, threshold, rl),
        quantize(g, threshold, gl),
        quantize(b, threshold, bl),
    )


def main() -> None:
    for y, row in enumerate(BAYER_4X4):
        for x, expected in enumerate(row):
            threshold = bayer4(x, y)
            assert isclose(threshold, (expected + 0.5) / 16.0)
            assert isclose(threshold, bayer4(x + 4, y + 4))
            assert 0.0 < threshold < 1.0

    assert cell_size(1) == 1
    assert cell_size(1080) == 4
    assert cell_size(1440) == 5
    assert cell_size(2160) == 8
    assert cell_size(1080) * 4 == 16
    assert cell_size(1440) * 4 == 20
    assert cell_size(2160) * 4 == 32

    assert cell_for_pixel(0.0, 0.0, 1080) == (0, 0)
    assert cell_for_pixel(3.99, 3.99, 1080) == (0, 0)
    assert cell_for_pixel(4.0, 4.0, 1080) == (1, 1)
    assert source_center(0.0, 0.0, 1920, 1080) == source_center(3.99, 3.99, 1920, 1080)
    assert source_center(5.0, 0.0, 6, 1080)[0] == 5.5

    for value in (-1.0, 0.0, 0.1, 0.5, 0.9, 1.0, 2.0):
        for levels in CHANNEL_LEVELS:
            for y in range(4):
                for x in range(4):
                    output = quantize(value, bayer4(x, y), levels)
                    assert isfinite(output)
                    assert 0.0 <= output <= 1.0
                    assert isclose(output * (levels - 1), round(output * (levels - 1)))

    expected_palette = {
        (red / 7.0, green / 7.0, blue / 3.0)
        for red in range(8)
        for green in range(8)
        for blue in range(4)
    }
    palette = {quantize_rgb(color, bayer4(0, 0)) for color in expected_palette}
    assert palette == expected_palette
    assert len(palette) == 256

    alpha = 0.37
    output = (*quantize_rgb((0.2, 0.5, 0.8), bayer4(0, 0)), alpha)
    assert output[3] == alpha

    print("Dithering self-check passed.")


if __name__ == "__main__":
    main()
