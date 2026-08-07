#include "polyizon/scene/entity_uuid.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

namespace polyizon {

namespace {

std::mt19937_64& Generator() {
    static std::mt19937_64 generator([] {
        std::random_device source;
        std::seed_seq seed{
            source(), source(), source(), source(),
            static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
        };
        return std::mt19937_64(seed);
    }());
    return generator;
}

std::mutex& GeneratorMutex() {
    static std::mutex mutex;
    return mutex;
}

bool IsHex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

} // namespace

std::string GenerateEntityUuid() {
    std::array<std::uint8_t, 16> bytes{};
    {
        std::scoped_lock lock(GeneratorMutex());
        const std::uint64_t high = Generator()();
        const std::uint64_t low = Generator()();
        for (int i = 0; i < 8; ++i) {
            bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(high >> (i * 8));
            bytes[static_cast<std::size_t>(i + 8)] = static_cast<std::uint8_t>(low >> (i * 8));
        }
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0fU]);
    }
    return result;
}

bool IsValidEntityUuid(std::string_view value) noexcept {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        if (separator ? value[i] != '-' : !IsHex(value[i])) return false;
    }
    return true;
}

} // namespace polyizon

