#include "polyizon/noise.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <random>

namespace polyizon {

namespace {

constexpr std::uint32_t kSeed = 1337; // fixed: deterministic cloud shape across runs

std::array<int, 512> BuildPermutationTable() {
    std::array<int, 256> p{};
    for (int i = 0; i < 256; ++i) {
        p[static_cast<std::size_t>(i)] = i;
    }
    std::mt19937 rng(kSeed);
    for (int i = 255; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        std::swap(p[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(dist(rng))]);
    }
    std::array<int, 512> perm{};
    for (int i = 0; i < 512; ++i) {
        perm[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(i & 255)];
    }
    return perm;
}

float Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Lerp(float t, float a, float b) {
    return a + t * (b - a);
}

float Grad(int hash, float x, float y, float z) {
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

int WrapInt(int v, int period) {
    return ((v % period) + period) % period;
}

// Tileable 3D Perlin (gradient) noise: lattice indices are wrapped mod
// `period` before hashing, so the gradient at lattice point (i,j,k) is
// identical to (i+period,j,k) etc — the noise repeats exactly every `period`
// units along each axis. Returns roughly [-1, 1].
float TileablePerlin3D(const std::array<int, 512>& perm, float x, float y, float z, int period) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const int zi = static_cast<int>(std::floor(z));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);
    const float zf = z - static_cast<float>(zi);

    const int x0 = WrapInt(xi, period), x1 = WrapInt(xi + 1, period);
    const int y0 = WrapInt(yi, period), y1 = WrapInt(yi + 1, period);
    const int z0 = WrapInt(zi, period), z1 = WrapInt(zi + 1, period);

    auto hash = [&perm](int hx, int hy, int hz) {
        return perm[static_cast<std::size_t>(
            (perm[static_cast<std::size_t>((perm[static_cast<std::size_t>(hx & 255)] + hy) & 255)] + hz) & 255)];
    };

    const float u = Fade(xf), v = Fade(yf), w = Fade(zf);

    const float n000 = Grad(hash(x0, y0, z0), xf, yf, zf);
    const float n100 = Grad(hash(x1, y0, z0), xf - 1.0f, yf, zf);
    const float n010 = Grad(hash(x0, y1, z0), xf, yf - 1.0f, zf);
    const float n110 = Grad(hash(x1, y1, z0), xf - 1.0f, yf - 1.0f, zf);
    const float n001 = Grad(hash(x0, y0, z1), xf, yf, zf - 1.0f);
    const float n101 = Grad(hash(x1, y0, z1), xf - 1.0f, yf, zf - 1.0f);
    const float n011 = Grad(hash(x0, y1, z1), xf, yf - 1.0f, zf - 1.0f);
    const float n111 = Grad(hash(x1, y1, z1), xf - 1.0f, yf - 1.0f, zf - 1.0f);

    const float nx00 = Lerp(u, n000, n100);
    const float nx10 = Lerp(u, n010, n110);
    const float nx01 = Lerp(u, n001, n101);
    const float nx11 = Lerp(u, n011, n111);
    const float nxy0 = Lerp(v, nx00, nx10);
    const float nxy1 = Lerp(v, nx01, nx11);
    return Lerp(w, nxy0, nxy1);
}

// Fractal Brownian motion over TileablePerlin3D. Each octave's own period
// grows with its own frequency multiplier in lockstep with its coordinate
// range (period = basePeriod * freq, coordinate = (x,y,z) * freq), so every
// octave tiles seamlessly regardless of the specific basePeriod/resolution
// chosen by the caller. Returns roughly [-1, 1].
float PerlinFbm3D(const std::array<int, 512>& perm, float x, float y, float z, int basePeriod, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float freq = 1.0f;
    float maxValue = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const int period = basePeriod * static_cast<int>(freq);
        sum += TileablePerlin3D(perm, x * freq, y * freq, z * freq, period) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        freq *= 2.0f;
    }
    return sum / maxValue;
}

// Tileable 3D Worley (F1/cellular) noise: one pseudo-random feature point
// per grid cell, cell coordinates wrapped mod `cellCount` before hashing so
// the same feature-point layout repeats every `cellCount` cells — checks the
// full 3x3x3 neighborhood (a feature point in a neighboring cell can still
// be the closest one). Returns distance in cell units, roughly [0, sqrt(3)].
float TileableWorley3D(const std::array<int, 512>& perm, float x, float y, float z, int cellCount) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const int zi = static_cast<int>(std::floor(z));

    float minDistSq = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = xi + dx;
                const int cy = yi + dy;
                const int cz = zi + dz;
                const int wcx = WrapInt(cx, cellCount);
                const int wcy = WrapInt(cy, cellCount);
                const int wcz = WrapInt(cz, cellCount);

                const int h1 = perm[static_cast<std::size_t>(
                    (perm[static_cast<std::size_t>((perm[static_cast<std::size_t>(wcx & 255)] + wcy) & 255)] + wcz) &
                    255)];
                const int h2 = perm[static_cast<std::size_t>((h1 + 1) & 511)];
                const int h3 = perm[static_cast<std::size_t>((h2 + 1) & 511)];

                const float fx = static_cast<float>(cx) + static_cast<float>(h1 & 255) / 255.0f;
                const float fy = static_cast<float>(cy) + static_cast<float>(h2 & 255) / 255.0f;
                const float fz = static_cast<float>(cz) + static_cast<float>(h3 & 255) / 255.0f;

                const float ddx = fx - x;
                const float ddy = fy - y;
                const float ddz = fz - z;
                minDistSq = std::min(minDistSq, ddx * ddx + ddy * ddy + ddz * ddz);
            }
        }
    }
    return std::sqrt(minDistSq);
}

float WorleyFbm3D(const std::array<int, 512>& perm, float x, float y, float z, int baseCellCount, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float freq = 1.0f;
    float maxValue = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const int cellCount = baseCellCount * static_cast<int>(freq);
        // Worley distance is naturally ~[0, sqrt(3)/freq] at this frequency;
        // invert+normalize so higher values mean "denser" (more billowy),
        // matching Perlin's convention above.
        const float d = TileableWorley3D(perm, x * freq, y * freq, z * freq, cellCount);
        sum += (1.0f - std::min(d, 1.0f)) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        freq *= 2.0f;
    }
    return sum / maxValue; // [0, 1]
}

float Remap(float value, float oldMin, float oldMax, float newMin, float newMax) {
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}

} // namespace

std::vector<std::uint8_t> GenerateCloudNoiseVolume(std::uint32_t resolution) {
    const auto startTime = std::chrono::steady_clock::now();

    const std::array<int, 512> perm = BuildPermutationTable();
    std::vector<std::uint8_t> data(static_cast<std::size_t>(resolution) * resolution * resolution * 4);

    for (std::uint32_t z = 0; z < resolution; ++z) {
        for (std::uint32_t y = 0; y < resolution; ++y) {
            for (std::uint32_t x = 0; x < resolution; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(resolution);
                const float v = static_cast<float>(y) / static_cast<float>(resolution);
                const float w = static_cast<float>(z) / static_cast<float>(resolution);

                // Base shape: a mid-frequency Perlin fbm eroded by an
                // inverted low-frequency Worley floor (per Schneider's
                // talk), producing billowy cloud-like puffs rather than
                // uniform Perlin noise.
                const float perlin = PerlinFbm3D(perm, u, v, w, /*basePeriod=*/4, /*octaves=*/4); // [-1,1]
                const float worleyLow = WorleyFbm3D(perm, u, v, w, /*baseCellCount=*/4, /*octaves=*/2);  // [0,1]
                const float baseShape = std::clamp(Remap(perlin, -(1.0f - worleyLow), 1.0f, 0.0f, 1.0f), 0.0f, 1.0f);

                // Erosion/detail octaves, increasing frequency, used
                // in-shader as a weighted blend (see sky.frag's
                // SampleCloudDensity).
                const float detail1 = WorleyFbm3D(perm, u, v, w, /*baseCellCount=*/8, /*octaves=*/2);
                const float detail2 = WorleyFbm3D(perm, u, v, w, /*baseCellCount=*/16, /*octaves=*/2);
                const float detail3 = WorleyFbm3D(perm, u, v, w, /*baseCellCount=*/32, /*octaves=*/2);

                const std::size_t index =
                    (static_cast<std::size_t>(z) * resolution * resolution + static_cast<std::size_t>(y) * resolution +
                        x) * 4;
                data[index + 0] = static_cast<std::uint8_t>(std::clamp(baseShape * 255.0f, 0.0f, 255.0f));
                data[index + 1] = static_cast<std::uint8_t>(std::clamp(detail1 * 255.0f, 0.0f, 255.0f));
                data[index + 2] = static_cast<std::uint8_t>(std::clamp(detail2 * 255.0f, 0.0f, 255.0f));
                data[index + 3] = static_cast<std::uint8_t>(std::clamp(detail3 * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    std::printf("[Noise] Generated %ux%ux%u cloud noise volume in %lld ms\n", resolution, resolution, resolution,
        static_cast<long long>(elapsedMs));
    std::fflush(stdout); // redirected/piped stdout is fully buffered — flush so this is visible immediately, not just at graceful exit

    return data;
}

} // namespace polyizon
