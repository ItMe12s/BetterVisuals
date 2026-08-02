from math import floor

REFERENCE_HEIGHT = 1080
BASE_BLOCK_SIZE = 4


def block_size(height: int) -> int:
    return max(floor(BASE_BLOCK_SIZE * height / REFERENCE_HEIGHT + 0.5), 1)


def sample_center(x: float, y: float, width: int, height: int) -> tuple[float, float]:
    size = block_size(height)
    center_x = (floor(x / size) + 0.5) * size
    center_y = (floor(y / size) + 0.5) * size
    return min(max(center_x, 0.5), width - 0.5), min(max(center_y, 0.5), height - 0.5)


def main() -> None:
    assert block_size(1) == 1
    assert block_size(1080) == 4
    assert block_size(1440) == 5
    assert block_size(2160) == 8

    assert sample_center(0.0, 0.0, 1920, 1080) == (2.0, 2.0)
    assert sample_center(0.0, 0.0, 1920, 1080) == sample_center(3.99, 3.99, 1920, 1080)
    assert sample_center(4.0, 4.0, 1920, 1080) == (6.0, 6.0)
    assert sample_center(5.0, 0.0, 6, 1080)[0] == 5.5

    print("Pixelate self-check passed.")


if __name__ == "__main__":
    main()
