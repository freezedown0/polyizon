#pragma once

#include <cstdint>
#include <vector>

namespace polyizon {

// Generates a tileable resolution^3 RGBA8 volume for the volumetric cloud
// raymarch (sky.frag): R = Perlin-Worley base cloud shape (billowy puffs,
// per Andrew Schneider's "Real-Time Volumetric Cloudscapes" GDC/SIGGRAPH
// talk), G/B/A = Worley fbm at increasing frequencies, used in-shader as
// erosion/detail layered on top of the base shape. A single combined
// texture — a deliberate simplification of Schneider's original separate
// base+detail textures, since this phase has no compute shader and
// generates entirely on the CPU at startup (see Texture3D).
//
// Every noise function used internally is tileable at this exact
// resolution (lattice/cell indices wrap mod resolution, or mod a divisor of
// it), which matters because the resulting texture is sampled with
// VK_SAMPLER_ADDRESS_MODE_REPEAT and continuously wind-scrolled in-shader —
// non-tileable noise would show a visible seam at the wrap boundary.
//
// Deterministic (fixed internal seed): there's no reason for the cloud
// shape to differ between runs, and determinism makes visual regressions
// reproducible.
std::vector<std::uint8_t> GenerateCloudNoiseVolume(std::uint32_t resolution);

} // namespace polyizon
