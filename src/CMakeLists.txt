set(SPIO_CORE_SOURCES
    SpioCLI/CLI.cpp
    SpioCompat/Compat.cpp
    SpioManifest/Lockfile.cpp
    SpioManifest/Manifest.cpp
    SpioPack/Pack.cpp
    SpioPlan/CompilePlan.cpp
    SpioResolve/Resolver.cpp
    SpioTree/Render.cpp
    SpioWorkflow/Dependencies.cpp
)

add_library(spio_core STATIC ${SPIO_CORE_SOURCES})
target_include_directories(spio_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(spio_core PUBLIC
  "SPIO_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
  "SPIO_PROJECT_VERSION=\"${PROJECT_VERSION}-dev\""
)
target_link_libraries(spio_core PUBLIC tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

add_executable(spio main.cpp)
target_link_libraries(spio PRIVATE spio_core)
