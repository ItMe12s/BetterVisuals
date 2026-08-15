from __future__ import annotations

from math import floor, isfinite

EPSILON = 1e-7


def sample_tap(
    image: list[list[tuple[float, float, float]]],
    fp: tuple[float, float],
    offset: tuple[float, float],
) -> tuple[float, float, float]:
    height, width = len(image), len(image[0])
    x = min(max(int(fp[0]) + int(offset[0]), 0), width - 1)
    y = min(max(int(fp[1]) + int(offset[1]), 0), height - 1)
    return image[y][x]


def tap_luma(color: tuple[float, float, float]) -> float:
    r, g, b = color
    return b * 0.5 + (r * 0.5 + g)


def accumulate_direction(
    dir: list[float],
    length: list[float],
    weight: float,
    lA: float,
    lB: float,
    lC: float,
    lD: float,
    lE: float,
) -> None:
    dc = lD - lC
    cb = lC - lB
    len_x = max(abs(dc), abs(cb))
    len_x = 1.0 / max(len_x, EPSILON)
    dir_x = lD - lB
    dir[0] += dir_x * weight
    len_x = min(max(abs(dir_x) * len_x, 0.0), 1.0)
    len_x *= len_x
    length[0] += len_x * weight

    ec = lE - lC
    ca = lC - lA
    len_y = max(abs(ec), abs(ca))
    len_y = 1.0 / max(len_y, EPSILON)
    dir_y = lE - lA
    dir[1] += dir_y * weight
    len_y = min(max(abs(dir_y) * len_y, 0.0), 1.0)
    len_y *= len_y
    length[0] += len_y * weight


def accumulate_tap(
    accumulation: list[float],
    weight_sum: list[float],
    offset: tuple[float, float],
    dir: tuple[float, float],
    len2: tuple[float, float],
    lob: float,
    clip: float,
    color: tuple[float, float, float],
) -> None:
    rotated_x = offset[0] * dir[0] + offset[1] * dir[1]
    rotated_y = offset[0] * (-dir[1]) + offset[1] * dir[0]
    rotated_x *= len2[0]
    rotated_y *= len2[1]
    d2 = rotated_x * rotated_x + rotated_y * rotated_y
    d2 = min(d2, clip)
    w_b = (2.0 / 5.0) * d2 - 1.0
    w_a = lob * d2 - 1.0
    w_b *= w_b
    w_a *= w_a
    w_b = (25.0 / 16.0) * w_b - (25.0 / 16.0 - 1.0)
    w = w_b * w_a
    for channel in range(3):
        accumulation[channel] += color[channel] * w
    weight_sum[0] += w


def easu_upscale(
    image: list[list[tuple[float, float, float]]], out_width: int, out_height: int
) -> list[list[tuple[float, float, float]]]:
    height, width = len(image), len(image[0])
    output: list[list[tuple[float, float, float]]] = []
    for out_y in range(out_height):
        row: list[tuple[float, float, float]] = []
        for out_x in range(out_width):
            pp_x = (out_x + 0.5) / out_width * width - 0.5
            pp_y = (out_y + 0.5) / out_height * height - 0.5
            fp = (floor(pp_x), floor(pp_y))
            pp = (pp_x - fp[0], pp_y - fp[1])

            taps = {
                "b": sample_tap(image, fp, (0.0, -1.0)),
                "c": sample_tap(image, fp, (1.0, -1.0)),
                "i": sample_tap(image, fp, (-1.0, 1.0)),
                "j": sample_tap(image, fp, (0.0, 1.0)),
                "f": sample_tap(image, fp, (0.0, 0.0)),
                "e": sample_tap(image, fp, (-1.0, 0.0)),
                "k": sample_tap(image, fp, (1.0, 1.0)),
                "l": sample_tap(image, fp, (2.0, 1.0)),
                "h": sample_tap(image, fp, (2.0, 0.0)),
                "g": sample_tap(image, fp, (1.0, 0.0)),
                "o": sample_tap(image, fp, (1.0, 2.0)),
                "n": sample_tap(image, fp, (0.0, 2.0)),
            }
            luma = {name: tap_luma(color) for name, color in taps.items()}

            direction = [0.0, 0.0]
            current_len = [0.0]
            accumulate_direction(
                direction,
                current_len,
                (1.0 - pp[0]) * (1.0 - pp[1]),
                luma["b"],
                luma["e"],
                luma["f"],
                luma["g"],
                luma["j"],
            )
            accumulate_direction(
                direction,
                current_len,
                pp[0] * (1.0 - pp[1]),
                luma["c"],
                luma["f"],
                luma["g"],
                luma["h"],
                luma["k"],
            )
            accumulate_direction(
                direction,
                current_len,
                (1.0 - pp[0]) * pp[1],
                luma["f"],
                luma["i"],
                luma["j"],
                luma["k"],
                luma["n"],
            )
            accumulate_direction(
                direction,
                current_len,
                pp[0] * pp[1],
                luma["g"],
                luma["j"],
                luma["k"],
                luma["l"],
                luma["o"],
            )

            dir_r = direction[0] * direction[0] + direction[1] * direction[1]
            zero = dir_r < (1.0 / 32768.0)
            dir_r = 1.0 if zero else 1.0 / max(dir_r, 1e-10) ** 0.5
            direction[0] = 1.0 if zero else direction[0]
            direction[0] *= dir_r
            direction[1] *= dir_r

            current_len = current_len[0] * 0.5
            current_len *= current_len
            stretch = (direction[0] * direction[0] + direction[1] * direction[1]) / max(
                max(abs(direction[0]), abs(direction[1])), EPSILON
            )
            len2 = (
                1.0 + (stretch - 1.0) * current_len,
                1.0 - 0.5 * current_len,
            )
            lob = 0.5 + (0.25 - 0.04 - 0.5) * current_len
            clip = 1.0 / max(lob, EPSILON)

            min4 = tuple(
                min(taps[name][channel] for name in ("f", "g", "j", "k"))
                for channel in range(3)
            )
            max4 = tuple(
                max(taps[name][channel] for name in ("f", "g", "j", "k"))
                for channel in range(3)
            )

            accumulation = [0.0, 0.0, 0.0]
            weight_sum = [0.0]
            for name, offset in (
                ("b", (0.0, -1.0)),
                ("c", (1.0, -1.0)),
                ("i", (-1.0, 1.0)),
                ("j", (0.0, 1.0)),
                ("f", (0.0, 0.0)),
                ("e", (-1.0, 0.0)),
                ("k", (1.0, 1.0)),
                ("l", (2.0, 1.0)),
                ("h", (2.0, 0.0)),
                ("g", (1.0, 0.0)),
                ("o", (1.0, 2.0)),
                ("n", (0.0, 2.0)),
            ):
                accumulate_tap(
                    accumulation,
                    weight_sum,
                    offset,
                    (direction[0], direction[1]),
                    len2,
                    lob,
                    clip,
                    taps[name],
                )

            clamped = lambda channel: min(
                max4[channel],
                max(min4[channel], accumulation[channel] * (1.0 / weight_sum[0])),
            )
            row.append((clamped(0), clamped(1), clamped(2)))
        output.append(row)
    return output


def gradient_image(width: int, height: int) -> list[list[tuple[float, float, float]]]:
    return [
        [
            (x / (width - 1), y / (height - 1), (x + y) / (width + height - 2))
            for x in range(width)
        ]
        for y in range(height)
    ]


def uniform_image(
    width: int, height: int, value: float
) -> list[list[tuple[float, float, float]]]:
    return [[(value, value, value) for _ in range(width)] for _ in range(height)]


def noisy_image(width: int, height: int) -> list[list[tuple[float, float, float]]]:
    image = gradient_image(width, height)
    for y in range(height):
        for x in range(width):
            r, g, b = image[y][x]
            image[y][x] = (
                min(1.0, max(0.0, r + 0.2 * ((x * 7 + y * 13) % 5) / 5.0)),
                g,
                b,
            )
    return image


def main() -> None:
    flat = uniform_image(8, 8, 0.37)
    flat_upscaled = easu_upscale(flat, 16, 16)
    for row in flat_upscaled:
        for color in row:
            assert color == (0.37, 0.37, 0.37)

    noisy = noisy_image(8, 8)
    for out_height, out_width in ((16, 16), (32, 32)):
        upscaled = easu_upscale(noisy, out_width, out_height)
        height, width = len(noisy), len(noisy[0])
        for y in range(out_height):
            for x in range(out_width):
                pp_x = (x + 0.5) / out_width * width - 0.5
                pp_y = (y + 0.5) / out_height * height - 0.5
                fp = (floor(pp_x), floor(pp_y))
                neighborhood = [
                    sample_tap(noisy, fp, (dx, dy))
                    for dx in (0.0, 1.0)
                    for dy in (0.0, 1.0)
                ]
                for channel in range(3):
                    minimum = min(color[channel] for color in neighborhood)
                    maximum = max(color[channel] for color in neighborhood)
                    value = upscaled[y][x][channel]
                    assert isfinite(value)
                    assert minimum - 1e-7 <= value <= maximum + 1e-7

    print("FSR 1 self-check passed.")


if __name__ == "__main__":
    main()
