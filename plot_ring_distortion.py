import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from scipy.integrate import solve_ivp
from scipy.optimize import brentq


# Simulation
FOV_DEG = 60.0
R_CAM = 1.9e11 / 1.269e10  # camera radius in Schwarzschild-radius units
TARGET_RGB = np.array([51, 153, 255])  # ring colour, RGB


# Theoretical
B_CRIT = 1.5 * np.sqrt(3)


def sweep(b, u0, u_source):
    up0 = np.sqrt(1 / b**2 - u0**2 * (1 - u0))
    rhs = lambda p, y: [y[1], -y[0] + 1.5 * y[0]**2]

    def event(p, y):
        return y[0] - u_source

    event.terminal = True
    event.direction = -1
    sol = solve_ivp(
        rhs, [0, 60], [u0, up0], events=event,
        rtol=1e-11, atol=1e-13
    )
    return sol.t_events[0][0] if len(sol.t_events[0]) else np.nan


def exact_ring_deg(r_obs, r_src):
    def equation(b):
        return np.nan_to_num(sweep(b, 1 / r_obs, 1 / r_src), nan=0.0) - np.pi

    b_max = r_obs / np.sqrt(1 - 1 / r_obs) * (1 - 1e-9)
    b = brentq(equation, B_CRIT * (1 + 1e-9), b_max, xtol=1e-10)
    return np.degrees(np.arcsin(b * np.sqrt(1 - 1 / r_obs) / r_obs))


def thin_lens_deg(r_obs, r_src):
    return np.degrees(np.sqrt(2 * r_src / (r_obs * (r_obs + r_src))))


# images
def read_ppm(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"P6":
            raise ValueError(f"{path}: expected a binary P6 PPM file")

        header_values = []
        while len(header_values) < 3:
            line = f.readline()
            if not line:
                raise ValueError(f"{path}: incomplete PPM header")
            line = line.split(b"#", 1)[0]
            header_values.extend(line.split())

        width, height, max_value = map(int, header_values[:3])
        if max_value != 255:
            raise ValueError(f"{path}: expected max colour value 255, got {max_value}")

        pixels = np.frombuffer(f.read(), dtype=np.uint8)
        expected = width * height * 3
        if pixels.size != expected:
            raise ValueError(
                f"{path}: expected {expected} pixel bytes, found {pixels.size}"
            )
        return pixels.reshape(height, width, 3)


def pixel_radius_to_deg(radius_px, image_height):
    angle = np.arctan(
        2 * radius_px * np.tan(np.radians(FOV_DEG / 2)) / image_height
    )
    return np.degrees(angle)


def measure_ring(path):
    match = re.search(r"ring_([0-9]+(?:\.[0-9]+)?)", Path(path).name)
    if not match:
        raise ValueError(f"Cannot read source distance from filename: {path}")
    distance = float(match.group(1))

    image = read_ppm(path)
    height, width, _ = image.shape
    mask = np.all(np.abs(image.astype(np.int16) - TARGET_RGB) <= 2, axis=2)
    ys, xs = np.nonzero(mask)

    if len(xs) == 0:
        print(f"Warning: no pixels matching the target ring colour in {path}")
        return None

    dx = xs + 0.5 - width / 2
    dy = ys + 0.5 - height / 2
    radii_px = np.hypot(dx, dy)

    inner = pixel_radius_to_deg(radii_px.min(), height)
    outer = pixel_radius_to_deg(radii_px.max(), height)
    midpoint = (inner + outer) / 2
    return distance, inner, outer, midpoint


def main():
    if len(sys.argv) > 1:
        files = [Path(p) for p in sys.argv[1:]]
    else:
        files = list(Path(".").glob("ring_*.ppm"))

    files = sorted(
        files,
        key=lambda p: float(re.search(r"ring_([0-9]+(?:\.[0-9]+)?)", p.name).group(1))
        if re.search(r"ring_([0-9]+(?:\.[0-9]+)?)", p.name) else float("inf")
    )

    if not files:
        raise SystemExit("No ring_*.ppm files found. Put this script beside the images.")

    measured = []
    for path in files:
        if not path.exists():
            print(f"Warning: file not found: {path}")
            continue
        result = measure_ring(path)
        if result is not None:
            measured.append(result)

    if not measured:
        raise SystemExit("No rings could be measured. Check the PPM files and TARGET_RGB.")

    measured.sort(key=lambda row: row[0])
    distances = np.array([row[0] for row in measured])
    inner = np.array([row[1] for row in measured])
    outer = np.array([row[2] for row in measured])
    mid = np.array([row[3] for row in measured])

    exact = np.array([exact_ring_deg(R_CAM, d) for d in distances])
    thin = np.array([thin_lens_deg(R_CAM, d) for d in distances])

    # Table
    print(f"{'Distance (r_s)':>15} {'Measured (deg)':>16} "
          f"{'Inner (deg)':>13} {'Outer (deg)':>13} "
          f"{'Exact theory':>14} {'Thin lens':>12}")
    for d, m, i, o, e, t in zip(distances, mid, inner, outer, exact, thin):
        print(f"{d:15.1f} {m:16.4f} {i:13.4f} {o:13.4f} {e:14.4f} {t:12.4f}")

    plt.figure(figsize=(8, 6))

    plt.plot(distances, mid, "o-", label="Measured ring midpoint")
    plt.plot(distances, exact, "s--", label="Exact Schwarzschild prediction")
    plt.plot(distances, thin, "^:", label="Thin-lens approximation")

    plt.xlabel("Source distance (Schwarzschild radii, $r_s$)")
    plt.ylabel("Ring angular radius (degrees)")
    plt.title("Ring radius vs. source distance")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
