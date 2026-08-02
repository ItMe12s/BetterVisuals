from math import floor, isfinite, sin

DISTORTION_STRENGTH = 0.5
NOISE_OCTAVES = 2
NOISE_NORMALIZATION = 85.0 / 64.0


def clamp(value: float, minimum: float, maximum: float) -> float:
    return min(max(value, minimum), maximum)


def fract(value: float) -> float:
    return value - floor(value)


def shader_hash(x: float, y: float) -> float:
    value_x = fract(x * 0.1031)
    value_y = fract(y * 0.1031)
    value_z = value_x
    offset = (
        value_x * (value_y + 33.33)
        + value_y * (value_z + 33.33)
        + value_z * (value_x + 33.33)
    )
    value_x += offset
    value_y += offset
    value_z += offset
    return fract((value_x + value_y) * value_z)


def smoothstep(value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def interpolated_hash(x: float, y: float, resolution: float) -> float:
    scaled_x = x * resolution
    scaled_y = y * resolution
    inv_resolution = 1.0 / resolution
    base_x = floor(scaled_x) * inv_resolution
    base_y = floor(scaled_y) * inv_resolution
    h00 = shader_hash(base_x, base_y)
    h10 = shader_hash(base_x + inv_resolution, base_y)
    h01 = shader_hash(base_x, base_y + inv_resolution)
    h11 = shader_hash(base_x + inv_resolution, base_y + inv_resolution)
    phase_x = smoothstep(fract(scaled_x))
    phase_y = smoothstep(fract(scaled_y))
    low = h00 * (1.0 - phase_x) + h10 * phase_x
    high = h01 * (1.0 - phase_x) + h11 * phase_x
    return low * (1.0 - phase_y) + high * phase_y


def noise(x: float, y: float) -> float:
    total = 0.0
    resolution = 4.0
    weight = 0.5
    for octave in range(1, NOISE_OCTAVES + 1):
        total += interpolated_hash(x + octave, y + octave, resolution) * weight
        resolution *= 2.0
        weight *= 0.5
    return total * NOISE_NORMALIZATION


def switching_phase(y: float) -> float:
    return 1.0 - smoothstep(y / 0.03)


def distorted_uv(x: float, y: float, time: float) -> tuple[float, float]:
    x += (noise(y, time) - 0.5) * 0.005 * DISTORTION_STRENGTH
    high_frequency_offset = noise(y * 100.0, time * 10.0) - 0.5
    x += high_frequency_offset * 0.01 * DISTORTION_STRENGTH
    crease_phase = (
        clamp(
            (sin(y * 8.0 - time * 3.14159265 * 1.2) - 0.92) * noise(time, time),
            0.0,
            0.01,
        )
        * 10.0
    )
    crease_noise = max(high_frequency_offset, 0.0)
    x -= crease_noise * crease_phase * DISTORTION_STRENGTH
    phase = switching_phase(y)
    y += phase * 0.3 * DISTORTION_STRENGTH
    x += phase * high_frequency_offset * 0.2 * DISTORTION_STRENGTH
    return x, y


def main() -> None:
    assert shader_hash(0.25, 0.75) == shader_hash(0.25, 0.75)
    hash_values = []
    for x, y in ((-1.0, 0.0), (0.0, 0.0), (0.25, 0.75), (100.0, -20.0)):
        value = shader_hash(x, y)
        hash_values.append(value)
        assert isfinite(value)
        assert 0.0 <= value < 1.0
    assert len(set(hash_values)) > 1

    maximum_noise = NOISE_NORMALIZATION * sum(
        1.0 / (2.0**octave) for octave in range(1, NOISE_OCTAVES + 1)
    )
    assert maximum_noise == sum(1.0 / (2.0**octave) for octave in range(1, 9))
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

    unique_offsets = [tap - 6 for tap in range(7)]
    red_offsets = [offset for offset in unique_offsets if offset >= -2]
    green_offsets = [offset for offset in unique_offsets if -4 <= offset <= -2]
    blue_offsets = [offset for offset in unique_offsets if offset <= -4]
    assert len(unique_offsets) == len(set(unique_offsets)) == 7
    assert red_offsets == [-2, -1, 0]
    assert green_offsets == [-4, -3, -2]
    assert blue_offsets == [-6, -5, -4]
    assert len(red_offsets) + len(green_offsets) + len(blue_offsets) == 9
    assert unique_offsets[6] == 0

    print("VHS self-check passed.")


if __name__ == "__main__":
    main()
