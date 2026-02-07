#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std::literals;

// =============================================================================
// spdlog stubs - logging implementation for tests
// =============================================================================
#include <iostream>
#include <format>

namespace spdlog {
    namespace detail {
        enum class level { trace, debug, info, warn, error, critical };

        inline const char* level_name(level lvl) {
            switch (lvl) {
                case level::trace: return "TRACE";
                case level::debug: return "DEBUG";
                case level::info: return "INFO";
                case level::warn: return "WARN";
                case level::error: return "ERROR";
                case level::critical: return "CRITICAL";
                default: return "???";
            }
        }

        template<typename... Args>
        inline void log(level lvl, std::format_string<Args...> fmt, Args&&... args) {
            std::cerr << "[" << level_name(lvl) << "] "
                      << std::format(fmt, std::forward<Args>(args)...) << std::endl;
        }

        inline void log(level lvl, const char* msg) {
            std::cerr << "[" << level_name(lvl) << "] " << msg << std::endl;
        }

        inline void log(level lvl, const std::string& msg) {
            std::cerr << "[" << level_name(lvl) << "] " << msg << std::endl;
        }
    }

    template<typename... Args>
    inline void trace(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::trace, fmt, std::forward<Args>(args)...);
    }
    inline void trace(const char* msg) { detail::log(detail::level::trace, msg); }
    inline void trace(const std::string& msg) { detail::log(detail::level::trace, msg); }

    template<typename... Args>
    inline void debug(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::debug, fmt, std::forward<Args>(args)...);
    }
    inline void debug(const char* msg) { detail::log(detail::level::debug, msg); }
    inline void debug(const std::string& msg) { detail::log(detail::level::debug, msg); }

    template<typename... Args>
    inline void info(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::info, fmt, std::forward<Args>(args)...);
    }
    inline void info(const char* msg) { detail::log(detail::level::info, msg); }
    inline void info(const std::string& msg) { detail::log(detail::level::info, msg); }

    template<typename... Args>
    inline void warn(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::warn, fmt, std::forward<Args>(args)...);
    }
    inline void warn(const char* msg) { detail::log(detail::level::warn, msg); }
    inline void warn(const std::string& msg) { detail::log(detail::level::warn, msg); }

    template<typename... Args>
    inline void error(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::error, fmt, std::forward<Args>(args)...);
    }
    inline void error(const char* msg) { detail::log(detail::level::error, msg); }
    inline void error(const std::string& msg) { detail::log(detail::level::error, msg); }

    template<typename... Args>
    inline void critical(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(detail::level::critical, fmt, std::forward<Args>(args)...);
    }
    inline void critical(const char* msg) { detail::log(detail::level::critical, msg); }
    inline void critical(const std::string& msg) { detail::log(detail::level::critical, msg); }
}

// =============================================================================
// SKSE stubs
// =============================================================================
namespace SKSE {
    class TaskInterface {
    public:
        template<typename F>
        void AddTask(F&& task) {
            task();  // Execute immediately in tests
        }
    };

    inline TaskInterface* GetTaskInterface() {
        static TaskInterface instance;
        return &instance;
    }
}

// =============================================================================
// REL stubs (for address relocation)
// =============================================================================
namespace REL {
    template<typename T>
    struct Offset {
        constexpr Offset(std::uintptr_t offset) : m_offset(offset) {}
        std::uintptr_t offset() const { return m_offset; }
        std::uintptr_t address() const { return m_offset; }
        std::uintptr_t m_offset;
    };

    template<typename T>
    struct Relocation {
        Relocation(std::uintptr_t addr) : m_address(addr) {}
        std::uintptr_t address() const { return m_address; }
        T operator*() const { return T{}; }
        std::uintptr_t m_address;
    };

    template<typename T>
    inline void safe_write(std::uintptr_t, T) {}
}

namespace RE {
    // === Math Types ===
    struct NiPoint3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        NiPoint3() = default;
        NiPoint3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        bool operator==(const NiPoint3& other) const {
            return x == other.x && y == other.y && z == other.z;
        }

        NiPoint3 operator+(const NiPoint3& other) const {
            return NiPoint3(x + other.x, y + other.y, z + other.z);
        }

        NiPoint3 operator-(const NiPoint3& other) const {
            return NiPoint3(x - other.x, y - other.y, z - other.z);
        }

        NiPoint3 operator*(float scalar) const {
            return NiPoint3(x * scalar, y * scalar, z * scalar);
        }

        float Length() const {
            return std::sqrt(x * x + y * y + z * z);
        }
    };

    struct NiMatrix3 {
        float entry[3][3] = {{1,0,0}, {0,1,0}, {0,0,1}};
    };

    struct NiTransform {
        NiMatrix3 rotate;
        NiPoint3 translate;
        float scale = 1.0f;
    };

    // === Scene Graph Types ===
    class NiAVObject {
    public:
        NiTransform local;
        NiTransform world;
        NiAVObject* parent = nullptr;
        virtual ~NiAVObject() = default;

        NiAVObject* GetObjectByName(std::string_view) { return this; }
    };

    class NiNode : public NiAVObject {
    public:
        NiAVObject* GetObjectByName(std::string_view) { return this; }
    };

    // === Form Types ===
    using FormID = std::uint32_t;
    using RefHandle = uint32_t;

    template<typename T>
    class NiPointer {
    public:
        NiPointer() : m_ptr(nullptr) {}
        NiPointer(T* p) : m_ptr(p) {}

        T* get() const { return m_ptr; }
        T* operator->() const { return m_ptr; }
        T& operator*() const { return *m_ptr; }
        explicit operator bool() const { return m_ptr != nullptr; }

    private:
        T* m_ptr;
    };

    struct ObjectRefHandle {
        uint32_t value = 0;

        ObjectRefHandle() = default;
        explicit ObjectRefHandle(uint32_t v) : value(v) {}

        bool operator!() const { return value == 0; }
        explicit operator bool() const { return value != 0; }
        uint32_t native_handle() const { return value; }
    };

    class TESForm {
    public:
        FormID formID = 0;
        virtual ~TESForm() = default;

        FormID GetFormID() const { return formID; }
        TESForm* GetBaseObject() { return this; }

        template<typename T>
        T* As() { return dynamic_cast<T*>(this); }
    };

    class TESObjectREFR : public TESForm {
    public:
        NiPoint3 GetPosition() const { return position; }
        void SetPosition(const NiPoint3& pos) { position = pos; }

        NiNode* Get3D() { return node; }
        void Set3D(NiNode* n) { node = n; }

        ObjectRefHandle GetHandle() const { return handle; }
        void SetHandle(ObjectRefHandle h) { handle = h; }

        static NiPointer<TESObjectREFR> LookupByHandle(RefHandle h) {
            auto it = s_handleMap.find(h);
            return it != s_handleMap.end() ? NiPointer<TESObjectREFR>(it->second) : NiPointer<TESObjectREFR>();
        }

        static void RegisterHandle(RefHandle h, TESObjectREFR* ref) {
            s_handleMap[h] = ref;
        }

        static void ClearHandles() {
            s_handleMap.clear();
        }

    private:
        NiPoint3 position;
        NiNode* node = nullptr;
        ObjectRefHandle handle;
        static inline std::map<uint32_t, TESObjectREFR*> s_handleMap;
    };

    class PlayerCharacter : public TESObjectREFR {
    public:
        static PlayerCharacter* GetSingleton() {
            static PlayerCharacter instance;
            return &instance;
        }

        NiNode* Get3D() { return &dummyNode; }

    private:
        NiNode dummyNode;
    };

    class TESDataHandler {
    public:
        static TESDataHandler* GetSingleton() {
            static TESDataHandler instance;
            return &instance;
        }

        TESForm* LookupForm(FormID formID, const std::string& pluginName) {
            auto key = pluginName + ":" + std::to_string(formID);
            auto it = forms.find(key);
            return it != forms.end() ? it->second : nullptr;
        }

        void RegisterForm(const std::string& pluginName, FormID formID, TESForm* form) {
            forms[pluginName + ":" + std::to_string(formID)] = form;
        }

        void ClearForms() { forms.clear(); }

    private:
        std::map<std::string, TESForm*> forms;
    };

    // === Papyrus Stubs ===
    namespace BSScript {
        class IVirtualMachine;
        class Variable;
    }
    using VMStackID = std::uint32_t;
    struct StaticFunctionTag {};
}
