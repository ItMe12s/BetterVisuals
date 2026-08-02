from math import floor, isclose, isfinite

BAYER_8X8 = (
    (0, 32, 8, 40, 2, 34, 10, 42),
    (48, 16, 56, 24, 50, 18, 58, 26),
    (12, 44, 4, 36, 14, 46, 6, 38),
    (60, 28, 52, 20, 62, 30, 54, 22),
    (3, 35, 11, 43, 1, 33, 9, 41),
    (51, 19, 59, 27, 49, 17, 57, 25),
    (15, 47, 7, 39, 13, 45, 5, 37),
    (63, 31, 55, 23, 61, 29, 53, 21),
)
COLOR_LEVELS = 8


def bayer2(x: int, y: int) -> int:
    if y < 1:
        return 0 if x < 1 else 2
    return 3 if x < 1 else 1


def bayer4_value(x: int, y: int) -> int:
    x %= 4
    y %= 4
    return 4 * bayer2(x % 2, y % 2) + bayer2(x // 2, y // 2)


def bayer8(x: int, y: int) -> float:
    x %= 8
    y %= 8
    value = 4 * bayer4_value(x % 4, y % 4) + bayer2(x // 4, y // 4)
    return (value + 0.5) / 64.0


def quantize(value: float, threshold: float) -> float:
    scaled = min(max(value, 0.0), 1.0) * (COLOR_LEVELS - 1)
    level = floor(scaled) + (1 if scaled - floor(scaled) >= threshold else 0)
    return min(level, COLOR_LEVELS - 1) / (COLOR_LEVELS - 1)


def main() -> None:
    for y, row in enumerate(BAYER_8X8):
        for x, expected in enumerate(row):
            threshold = bayer8(x, y)
            assert isclose(threshold, (expected + 0.5) / 64.0)
            assert isclose(threshold, bayer8(x + 8, y + 8))
            assert 0.0 < threshold < 1.0

    for value in (-1.0, 0.0, 0.1, 0.5, 0.9, 1.0, 2.0):
        for y in range(8):
            for x in range(8):
                output = quantize(value, bayer8(x, y))
                assert isfinite(output)
                assert 0.0 <= output <= 1.0
                assert isclose(
                    output * (COLOR_LEVELS - 1), round(output * (COLOR_LEVELS - 1))
                )

    print("Dithering self-check passed.")


if __name__ == "__main__":
    main()
