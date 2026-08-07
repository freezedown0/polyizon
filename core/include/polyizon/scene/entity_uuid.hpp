#pragma once

#include <string>
#include <string_view>

namespace polyizon {

// RFC 4122-style random UUID used as a stable serialized entity identity.
// EnTT entity values remain transient runtime handles and are never written
// to disk or referenced by authored data.
std::string GenerateEntityUuid();
bool IsValidEntityUuid(std::string_view value) noexcept;

} // namespace polyizon

