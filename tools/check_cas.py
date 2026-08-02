from itertools import product
from math import isclose, isfinite, sqrt


def peak(strength: float) -> float:
    strength = min(max(strength, 0.0), 1.0)
    return -1.0 / (8.0 - 3.0 * strength)


def filter_channel(
    center: float,
    north: float,
    south: float,
    east: float,
    west: float,
    strength: float,
) -> float:
    center, north, south, east, west = (
        value * value for value in (center, north, south, east, west)
    )
    minimum = min(center, north, south, east, west)
    maximum = max(center, north, south, east, west)
    amplify = sqrt(min(max(min(minimum, 1.0 - maximum) / max(maximum, 1e-5), 0.0), 1.0))
    weight = amplify * peak(strength)
    filtered = ((north + south + east + west) * weight + center) / (1.0 + 4.0 * weight)
    return sqrt(min(max(filtered, 0.0), 1.0))


def main() -> None:
    assert isclose(peak(0.0), -0.125)
    assert isclose(peak(1.0), -0.2)

    for value in (0.0, 0.25, 0.5, 1.0):
        output = filter_channel(value, value, value, value, value, 1.0)
        assert isclose(output, value, abs_tol=1e-7)

    center = 0.6
    neighbors = (0.5, 0.5, 0.5, 0.5)
    low = filter_channel(center, *neighbors, 0.0)
    high = filter_channel(center, *neighbors, 1.0)
    assert abs(high - center) > abs(low - center)

    for center, north, south, east, west in product(
        (0.0, 0.25, 0.5, 0.75, 1.0), repeat=5
    ):
        for strength in (0.0, 0.5, 1.0):
            output = filter_channel(center, north, south, east, west, strength)
            assert isfinite(output)
            assert 0.0 <= output <= 1.0

    print("CAS self-check passed.")


if __name__ == "__main__":
    main()
