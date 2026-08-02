from math import isclose, isfinite

KERNEL_RADIUS = 6
KERNEL_SAMPLES = (KERNEL_RADIUS * 2 + 1) ** 2
THRESHOLD = 0.70
INTENSITY = 0.30
REFERENCE_HEIGHT = 1080


def clamp(value: float) -> float:
    return min(max(value, 0.0), 1.0)


def bright_pass(value: float) -> float:
    return clamp((value - THRESHOLD) / (1.0 - THRESHOLD))


def bloom(source: float, samples: list[float]) -> float:
    glow = sum(bright_pass(value) for value in samples) / KERNEL_SAMPLES
    return clamp(source + glow * INTENSITY)


def kernel_radius_pixels(height: int) -> float:
    return KERNEL_RADIUS * height / REFERENCE_HEIGHT


def main() -> None:
    assert KERNEL_SAMPLES == 169
    assert isclose(sum(1.0 / KERNEL_SAMPLES for _ in range(KERNEL_SAMPLES)), 1.0)
    assert bright_pass(THRESHOLD) == 0.0
    assert isclose(bright_pass(1.0), 1.0)
    assert isclose(kernel_radius_pixels(1080), 6.0)
    assert isclose(kernel_radius_pixels(1440), 8.0)
    assert isclose(kernel_radius_pixels(2160), 12.0)

    for source in (0.0, 0.5, 1.0):
        for sample in (-1.0, 0.0, THRESHOLD, 0.85, 1.0, 10.0):
            output = bloom(source, [sample] * KERNEL_SAMPLES)
            assert isfinite(output)
            assert 0.0 <= output <= 1.0

    print("Bloom self-check passed.")


if __name__ == "__main__":
    main()
