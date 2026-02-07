# VR-Editor Unit Tests

## Quick Start

```powershell
# Configure (first time or after CMakeLists.txt changes)
cd skse\VR-Editor
mkdir build -Force
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
         -DVCPKG_OVERLAY_TRIPLETS="../cmake" `
         -DVCPKG_TARGET_TRIPLET=x64-windows-skse `
         -G "Visual Studio 17 2022" -A x64

# Build tests
cmake --build . --config Release --target VREditorTests

# Run tests
ctest --output-on-failure -C Release
```

## Adding a New Test

1. Create a test file in `Tests/` (e.g., `Tests/test_myfeature.cpp`):

```cpp
#include <catch2/catch_all.hpp>
#include "MyHeader.h"  // Your source header

TEST_CASE("Feature description", "[tag]") {
    SECTION("Specific behavior") {
        REQUIRE(actual == expected);
    }
}
```

2. If your code uses CommonLib types, include TestStubs.h and guard your includes:

```cpp
#ifdef TEST_ENVIRONMENT
#include "TestStubs.h"
#else
#include "RE/Skyrim.h"
#endif

#include <catch2/catch_all.hpp>
#include "MyHeader.h"
```

3. Tests are auto-discovered - just rebuild.

## CommonLib Stubs

`Tests/TestStubs.h` provides minimal stubs for CommonLib classes (`RE::NiPoint3`, `RE::TESObjectREFR`, etc.) so tests compile without Skyrim dependencies.

To add new stubs, reference the real implementations in `skse/zreference/CommonLibSSE-NG/`.

## Test Tags

Use tags to organize and filter tests:
- `[placeholder]` - Infrastructure validation
- `[math]` - Math utilities
- `[transform]` - Transform operations

Run specific tags:
```powershell
.\Release\VREditorTests.exe "[math]"
```

## Disabling Tests

Build without tests:
```powershell
cmake .. -DBUILD_TESTS=OFF ...
```
