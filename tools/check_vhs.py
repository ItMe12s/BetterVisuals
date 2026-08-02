from math import floor, isfinite, sin

DISTORTION_STRENGTH = 0.5


def clamp(value: float, minimum: float, maximum: float) -> float:
    return min(max(value, minimum), maximum)


def fract(value: float) -> float:
    return value - floor(value)


def shader_hash(x: float, y: float) -> float:
    return fract(sin(x * 89.44 + y * 19.36) * 22189.22)


def smoothstep(value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def interpolated_hash(x: float, y: float, resolution: float) -> float:
    scaled_x = x * resolution
    scaled_y = y * resolution
    h00 = shader_hash(floor(scaled_x) / resolution, floor(scaled_y) / resolution)
    h10 = shader_hash(floor(scaled_x + 1.0) / resolution, floor(scaled_y) / resolution)
    h01 = shader_hash(floor(scaled_x) / resolution, floor(scaled_y + 1.0) / resolution)
    h11 = shader_hash(
        floor(scaled_x + 1.0) / resolution,
        floor(scaled_y + 1.0) / resolution,
    )
    phase_x = smoothstep(fract(scaled_x))
    phase_y = smoothstep(fract(scaled_y))
    low = h00 * (1.0 - phase_x) + h10 * phase_x
    high = h01 * (1.0 - phase_x) + h11 * phase_x
    return low * (1.0 - phase_y) + high * phase_y


def noise(x: float, y: float) -> float:
    total = 0.0
    for octave in range(1, 9):
        frequency = 2.0**octave
        total += interpolated_hash(x + octave, y + octave, 2.0 * frequency) / frequency
    return total


def switching_phase(y: float) -> float:
    return 1.0 - smoothstep(y / 0.03)


def distorted_uv(x: float, y: float, time: float) -> tuple[float, float]:
    source_y = y
    x += (noise(y, time) - 0.5) * 0.005 * DISTORTION_STRENGTH
    x += (noise(y * 100.0, time * 10.0) - 0.5) * 0.01 * DISTORTION_STRENGTH
    crease_phase = (
        clamp(
            (sin(y * 8.0 - time * 3.14159265 * 1.2) - 0.92) * noise(time, time),
            0.0,
            0.01,
        )
        * 10.0
    )
    crease_noise = max(noise(y * 100.0, time * 10.0) - 0.5, 0.0)
    x -= crease_noise * crease_phase * DISTORTION_STRENGTH
    phase = switching_phase(y)
    y += phase * 0.3 * DISTORTION_STRENGTH
    x += (
        phase
        * ((noise(source_y * 100.0, time * 10.0) - 0.5) * 0.2)
        * DISTORTION_STRENGTH
    )
    return x, y


def main() -> None:
    assert shader_hash(0.25, 0.75) == shader_hash(0.25, 0.75)
    for x, y in ((-1.0, 0.0), (0.0, 0.0), (0.25, 0.75), (100.0, -20.0)):
        value = shader_hash(x, y)
        assert isfinite(value)
        assert 0.0 <= value < 1.0

    maximum_noise = sum(1.0 / (2.0**octave) for octave in range(1, 9))
    for x, y in ((0.0, 0.0), (0.1, 1.0), (10.0, 25.0), (-2.0, 3.0)):
        value = noise(x, y)
        assert isfinite(value)
        assert 0.0 <= value <= maximum_noise

    for y in (-1.0, 0.0, 0.015, 0.03, 1.0):
        phase = switching_phase(y)
        assert 0.0 <= phase <= 1.0

    for x, y, time in ((0.0, 0.0, 0.0), (0.5, 0.5, 1.0), (1.0, 1.0, 60.0)):
        distorted_x, distorted_y = distorted_uv(x, y, time)
        assert isfinite(distorted_x)
        assert isfinite(distorted_y)

    bloom_offsets = [tap - 4 for tap in range(7)]
    assert bloom_offsets == [-4, -3, -2, -1, 0, 1, 2]

    print("VHS self-check passed.")


if __name__ == "__main__":
    main()
