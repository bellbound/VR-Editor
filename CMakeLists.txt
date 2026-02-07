# It's recommended to set a minimum CMake version.
# If you use CMake features from higher versions, update this to match.
cmake_minimum_required(VERSION 3.21)

# Set your project name. This will be the name of your SKSE .dll file.
project(VREditor VERSION 0.0.1 LANGUAGES CXX)

#

# YOU DO NOT NEED TO EDIT ANYTHING BELOW HERE
#
# Please see the "Project setup" section of README.md
#

# If you're not using a mod manager, you probably want the SKSE plugin to go
# inside of your Skyrim "Data" folder.
#
# To do this automatically, set the `SKYRIM_FOLDER` environment variable
# to the path of your Skyrim Special Edition folder
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

# If you're using Mod Organizer 2 or Vortex, you might want this to go inside
# of your "mods" folder, inside of a subfolder named "<your mod>".
#
# To do this automatically, set the `SKYRIM_MODS_FOLDER` environment variable
# to the path of your "mods" folder
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME} ${CMAKE_BUILD_TYPE}")
endif()

# Otherwise, you can set OUTPUT_FOLDER to any place you'd like :)
# set(OUTPUT_FOLDER "C:/path/to/any/folder")

# Setup your SKSE plugin as an SKSE plugin!
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) # <--- use C++23 standard
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h) # <--- PCH.h is required!

string(TIMESTAMP VREDIT_BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP VREDIT_BUILD_TIME "%H:%M:%S")

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		${SIMPLEINI_INCLUDE_DIRS}
		${CMAKE_SOURCE_DIR}/external
		${CMAKE_SOURCE_DIR}/include
)

# Define export macro for DLL API
target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE
		IN_GAME_PATCHER_EXPORTS
		VREDIT_BUILD_DATE="${VREDIT_BUILD_DATE}"
		VREDIT_BUILD_TIME="${VREDIT_BUILD_TIME}"
)

# Generate PDB in Release builds
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
endif()

# =============================================================================
# Unit Tests
# =============================================================================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    enable_testing()

    # Collect test sources
    file(GLOB_RECURSE TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/Tests/*.cpp"
    )

    # Create test executable
    add_executable(${PROJECT_NAME}Tests
        ${TEST_SOURCES}
    )

    target_compile_features(${PROJECT_NAME}Tests PRIVATE cxx_std_23)

    # Define TEST_ENVIRONMENT to use stubs instead of RE/Skyrim.h
    target_compile_definitions(${PROJECT_NAME}Tests PRIVATE TEST_ENVIRONMENT)

    target_include_directories(${PROJECT_NAME}Tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/Tests  # For TestStubs.h
    )

    # Ensure MSVC uses standard conformance mode
    if(MSVC)
        target_compile_options(${PROJECT_NAME}Tests PRIVATE /permissive- /Zc:preprocessor)
    endif()

    target_link_libraries(${PROJECT_NAME}Tests PRIVATE
        Catch2::Catch2WithMain
    )

    # Register tests with CTest
    include(Catch)
    catch_discover_tests(${PROJECT_NAME}Tests)
endif()
