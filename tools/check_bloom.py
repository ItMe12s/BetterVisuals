from math import isclose, isfinite

KERNEL_RADIUS = 3
BLOOM_RADIUS_AT_1080P = 8.0
KERNEL_TAPS = KERNEL_RADIUS * 2 + 1
KERNEL_SAMPLES = KERNEL_TAPS**2
KERNEL_WEIGHTS = (0.070159, 0.131075, 0.190713, 0.216106, 0.190713, 0.131075, 0.070159)
PREFILTER_TAPS = 1
BLUR_FETCHES = (0.0, 1.40733, 3.0)
BLUR_FETCHES_PER_PASS = 1 + 2 * (len(BLUR_FETCHES) - 1)
BLUR_ITERATIONS = 2
DOWNSAMPLE_FACTOR = 2
EQUIVALENT_FETCHES_PER_PIXEL = (
    2
    + (PREFILTER_TAPS + 2 * BLUR_ITERATIONS * BLUR_FETCHES_PER_PASS)
    / DOWNSAMPLE_FACTOR**2
)
THRESHOLD = 0.70
INTENSITY = 0.30
REFERENCE_HEIGHT = 1080


def clamp(value: float) -> float:
    return min(max(value, 0.0), 1.0)


def bright_pass(value: float, threshold: float = THRESHOLD) -> float:
    return clamp((value - threshold) / max(1.0 - threshold, 0.001))


def prefilter(samples: list[float]) -> float:
    return bright_pass(sum(samples) / len(samples))


def bloom(source: float, samples: list[float], intensity: float = INTENSITY) -> float:
    glow = sum(
        samples[y * KERNEL_TAPS + x] * KERNEL_WEIGHTS[x] * KERNEL_WEIGHTS[y]
        for y in range(KERNEL_TAPS)
        for x in range(KERNEL_TAPS)
    )
    return clamp(source + glow * intensity)


def linear_blur_samples():
    center = KERNEL_WEIGHTS[3]
    middle = KERNEL_WEIGHTS[4] + KERNEL_WEIGHTS[5]
    offset = 1.0 + KERNEL_WEIGHTS[5] / middle
    outer = KERNEL_WEIGHTS[6]
    return {
        0.0: center,
        offset: middle,
        3.0: outer,
    }


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
    assert PREFILTER_TAPS == 1
    assert BLUR_ITERATIONS == 2
    assert isclose(EQUIVALENT_FETCHES_PER_PIXEL, 7.25)
    assert bright_pass(THRESHOLD) == 0.0
    assert isclose(bright_pass(1.0), 1.0)
    assert bright_pass(0.5, 0.5) == 0.0
    assert isclose(bright_pass(1.0, 0.5), 1.0)
    assert bright_pass(0.99, 0.99) == 0.0
    assert bright_pass(1.0, 1.0) == 0.0
    assert isclose(bright_pass(0.9, 0.2), 0.875)
    assert isclose(kernel_radius_pixels(1080), 8.0)
    assert isclose(kernel_radius_pixels(1440), 32.0 / 3.0)
    assert isclose(kernel_radius_pixels(2160), 16.0)
    assert prefilter_pixel_pairs(4) == [(0, 1), (2, 3)]
    assert prefilter_pixel_pairs(5) == [(0, 1), (2, 3), (4, 4)]

    samples = linear_blur_samples()
    midpoint = 1.0 + KERNEL_WEIGHTS[5] / (KERNEL_WEIGHTS[4] + KERNEL_WEIGHTS[5])
    assert isclose(list(samples)[1], midpoint)
    assert isclose(samples[0] + 2.0 * (samples[midpoint] + samples[3.0]), 1.0)
    assert isclose(samples[0], KERNEL_WEIGHTS[3])
    assert isclose(samples[3.0], KERNEL_WEIGHTS[6])
    pair = samples[midpoint]
    assert isclose(pair * (2.0 - midpoint), KERNEL_WEIGHTS[4])
    assert isclose(pair * (midpoint - 1.0), KERNEL_WEIGHTS[5])

    impulse = prefilter([1.0, 0.0, 0.0, 0.0])
    assert isclose(impulse, bright_pass(0.25))
    brightBlock = prefilter([1.0, 1.0, 1.0, 1.0])
    assert isclose(brightBlock, 1.0)
    assert bloom(0.0, [brightBlock] + [0.0] * (KERNEL_SAMPLES - 1)) > 0.0

    for source in (0.0, 0.5, 1.0):
        for sample in (-1.0, 0.0, THRESHOLD, 0.85, 1.0, 10.0):
            downsampled = prefilter([sample] * PREFILTER_TAPS)
            output = bloom(source, [downsampled] * KERNEL_SAMPLES)
            assert isfinite(output)
            assert 0.0 <= output <= 1.0

    print("Bloom self-check passed.")


if __name__ == "__main__":
    main()
