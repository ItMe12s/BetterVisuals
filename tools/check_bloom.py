from math import isclose, isfinite

KERNEL_RADIUS = 3
BLOOM_RADIUS_AT_1080P = 8.0
KERNEL_TAPS = KERNEL_RADIUS * 2 + 1
KERNEL_SAMPLES = KERNEL_TAPS**2
KERNEL_WEIGHTS = (0.070159, 0.131075, 0.190713, 0.216106, 0.190713, 0.131075, 0.070159)
DOWNSAMPLE_FACTOR = 2
PREFILTER_TAPS = DOWNSAMPLE_FACTOR**2
EQUIVALENT_FETCHES_PER_PIXEL = (
    2 + (PREFILTER_TAPS + 2 * KERNEL_TAPS) / DOWNSAMPLE_FACTOR**2
)
THRESHOLD = 0.70
INTENSITY = 0.30
REFERENCE_HEIGHT = 1080


def clamp(value: float) -> float:
    return min(max(value, 0.0), 1.0)


def bright_pass(value: float) -> float:
    return clamp((value - THRESHOLD) / (1.0 - THRESHOLD))


def prefilter(samples: list[float]) -> float:
    return sum(bright_pass(value) for value in samples) / PREFILTER_TAPS


def bloom(source: float, samples: list[float]) -> float:
    glow = sum(
        samples[y * KERNEL_TAPS + x] * KERNEL_WEIGHTS[x] * KERNEL_WEIGHTS[y]
        for y in range(KERNEL_TAPS)
        for x in range(KERNEL_TAPS)
    )
    return clamp(source + glow * INTENSITY)


def kernel_radius_pixels(height: int) -> float:
    return BLOOM_RADIUS_AT_1080P * height / REFERENCE_HEIGHT


def prefilter_pixel_pairs(size: int) -> list[tuple[int, int]]:
    return [
        (2 * index, min(2 * index + 1, size - 1)) for index in range((size + 1) // 2)
    ]


def main() -> None:
    assert KERNEL_TAPS == 7
    assert KERNEL_SAMPLES == 49
    assert isclose(sum(KERNEL_WEIGHTS), 1.0)
    assert KERNEL_WEIGHTS[3] > KERNEL_WEIGHTS[2] > KERNEL_WEIGHTS[1] > KERNEL_WEIGHTS[0]
    assert PREFILTER_TAPS == 4
    assert isclose(EQUIVALENT_FETCHES_PER_PIXEL, 6.5)
    assert bright_pass(THRESHOLD) == 0.0
    assert isclose(bright_pass(1.0), 1.0)
    assert isclose(kernel_radius_pixels(1080), 8.0)
    assert isclose(kernel_radius_pixels(1440), 32.0 / 3.0)
    assert isclose(kernel_radius_pixels(2160), 16.0)
    assert prefilter_pixel_pairs(4) == [(0, 1), (2, 3)]
    assert prefilter_pixel_pairs(5) == [(0, 1), (2, 3), (4, 4)]

    impulse = prefilter([1.0, 0.0, 0.0, 0.0])
    assert isclose(impulse, 0.25)
    assert bloom(0.0, [impulse] + [0.0] * (KERNEL_SAMPLES - 1)) > 0.0

    for source in (0.0, 0.5, 1.0):
        for sample in (-1.0, 0.0, THRESHOLD, 0.85, 1.0, 10.0):
            downsampled = prefilter([sample] * PREFILTER_TAPS)
            output = bloom(source, [downsampled] * KERNEL_SAMPLES)
            assert isfinite(output)
            assert 0.0 <= output <= 1.0

    print("Bloom self-check passed.")


if __name__ == "__main__":
    main()
