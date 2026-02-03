#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <random>

namespace Util {

// Simple UUID implementation for unique object identification
// Format: 64-bit value combining monotonic counter with random component
// The upper 32 bits are a monotonically increasing counter (guarantees ordering)
// The lower 32 bits are random (helps distinguish concurrent creations)
class UUID {
public:
    UUID() : m_value(0) {}
    explicit UUID(uint64_t value) : m_value(value) {}

    static UUID Generate() {
        // Counter is atomic - safe for multi-threaded access
        static std::atomic<uint32_t> s_counter{0};

        // RNG is NOT thread-safe, use thread_local to give each thread its own instance
        thread_local std::random_device t_rd;
        thread_local std::mt19937 t_gen(t_rd());
        thread_local std::uniform_int_distribution<uint32_t> t_dist(0, UINT32_MAX);

        uint64_t upper = static_cast<uint64_t>(s_counter.fetch_add(1, std::memory_order_relaxed)) << 32;
        uint64_t lower = static_cast<uint64_t>(t_dist(t_gen));
        return UUID(upper | lower);
    }

    static UUID Invalid() { return UUID(0); }

    bool IsValid() const { return m_value != 0; }
    uint64_t Value() const { return m_value; }

    std::string ToString() const {
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(m_value));
        return std::string(buf);
    }

    bool operator==(const UUID& other) const { return m_value == other.m_value; }
    bool operator!=(const UUID& other) const { return m_value != other.m_value; }
    bool operator<(const UUID& other) const { return m_value < other.m_value; }

    struct Hash {
        size_t operator()(const UUID& uuid) const {
            return std::hash<uint64_t>{}(uuid.m_value);
        }
    };

private:
    uint64_t m_value;
};

// Type alias for action IDs
using ActionId = UUID;

} // namespace Util
