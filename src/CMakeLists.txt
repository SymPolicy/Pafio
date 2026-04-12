set(SPIO_CORE_SOURCES
    SpioCLI/CLI.cpp
    SpioCompat/Compat.cpp
    SpioCore/Paths.cpp
    SpioCore/Sha256.cpp
    SpioManifest/Lockfile.cpp
    SpioManifest/Manifest.cpp
    SpioPack/Pack.cpp
    SpioPlan/CompilePlan.cpp
    SpioPublish/Publish.cpp
    SpioRegistryClient/Client.cpp
    SpioRegistryServer/FilesystemPublish.cpp
    SpioRegistryServer/RemotePublish.cpp
    SpioResolve/Resolver.cpp
    SpioSecurity/RegistrySecurity.cpp
    SpioTool/Install.cpp
    SpioTree/Render.cpp
    SpioVendor/Vendor.cpp
    SpioWorkflow/Dependencies.cpp
)

file(GLOB_RECURSE SPIO_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src-private/*.cpp")
if(SPIO_PRIVATE_SOURCES)
  list(APPEND SPIO_CORE_SOURCES ${SPIO_PRIVATE_SOURCES})
endif()

add_library(spio_core STATIC ${SPIO_CORE_SOURCES})
target_include_directories(spio_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
if(EXISTS "${CMAKE_SOURCE_DIR}/src-private")
  target_include_directories(spio_core PRIVATE "${CMAKE_SOURCE_DIR}/src-private")
endif()
target_compile_definitions(spio_core PUBLIC
  "SPIO_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
  "SPIO_PROJECT_VERSION=\"${PROJECT_VERSION}-dev\""
)
target_link_libraries(spio_core PUBLIC tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

add_executable(spio main.cpp)
target_link_libraries(spio PRIVATE spio_core)
