#pragma once

#include <cstdint>
#include <vector>

namespace polyizon {

// Every tunable that previously lived as a hardcoded literal inside
// GenerateCloudNoiseVolume's body — pulled out so a developer (eventually
// via an editor panel) can edit cloud "look" without touching code, and so
// LoadOrGenerateCloudNoiseVolume has something concrete to hash for its
// disk cache. Field names match the noise call they parameterize (see
// noise.cpp): baseShape is the Perlin fbm eroded by an inverted low-freq
// Worley floor; detail1/2/3 are the increasing-frequency Worley erosion
// octaves layered on top in-shader (see sky.frag's SampleCloudDensity).
struct CloudNoiseParams {
    std::uint32_t resolution = 128;
    std::uint32_t seed = 1337;

    int baseShapePeriod = 4;
    int baseShapeOctaves = 4;
    int worleyLowCellCount = 4;
    int worleyLowOctaves = 2;

    int detail1CellCount = 8;
    int detail1Octaves = 2;
    int detail2CellCount = 16;
    int detail2Octaves = 2;
    int detail3CellCount = 32;
    int detail3Octaves = 2;
};

// Generates a tileable resolution^3 RGBA8 volume for the volumetric cloud
// raymarch (sky.frag): R = Perlin-Worley base cloud shape (billowy puffs,
// per Andrew Schneider's "Real-Time Volumetric Cloudscapes" GDC/SIGGRAPH
// talk), G/B/A = Worley fbm at increasing frequencies, used in-shader as
// erosion/detail layered on top of the base shape. A single combined
// texture — a deliberate simplification of Schneider's original separate
// base+detail textures, since this phase has no compute shader and
// generates entirely on the CPU (see Texture3D).
//
// Every noise function used internally is tileable at this exact
// resolution (lattice/cell indices wrap mod resolution, or mod a divisor of
// it), which matters because the resulting texture is sampled with
// VK_SAMPLER_ADDRESS_MODE_REPEAT and continuously wind-scrolled in-shader —
// non-tileable noise would show a visible seam at the wrap boundary.
//
// Deterministic for a given CloudNoiseParams (params.seed fixes the
// permutation table): identical params always produce identical output,
// which is what makes LoadOrGenerateCloudNoiseVolume's disk cache correct.
std::vector<std::uint8_t> GenerateCloudNoiseVolume(const CloudNoiseParams& params);

// Same result as GenerateCloudNoiseVolume(params), but checks a disk cache
// first (keyed by a hash of every CloudNoiseParams field) and only pays the
// CPU generation cost — several seconds at the default 128^3 resolution —
// on a cache miss (first launch with a given set of params, or after a
// developer edits them). Writes the cache after a miss so the next launch
// with the same params is near-instant.
std::vector<std::uint8_t> LoadOrGenerateCloudNoiseVolume(const CloudNoiseParams& params);

} // namespace polyizon
