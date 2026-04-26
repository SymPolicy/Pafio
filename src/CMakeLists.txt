add_library(spio_build_config INTERFACE)
target_include_directories(spio_build_config INTERFACE "${CMAKE_SOURCE_DIR}/src")
if(EXISTS "${CMAKE_SOURCE_DIR}/src-private")
  target_include_directories(spio_build_config INTERFACE "${CMAKE_SOURCE_DIR}/src-private")
endif()
target_compile_definitions(spio_build_config INTERFACE
  "SPIO_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
  "SPIO_PROJECT_VERSION=\"${PROJECT_VERSION}-dev\""
)
target_link_libraries(spio_build_config INTERFACE tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

function(spio_add_internal_static_library target)
  add_library(${target} STATIC ${ARGN})
  target_link_libraries(${target} PUBLIC spio_build_config)
endfunction()

set(SPIO_FOUNDATION_SOURCES
    SpioCompat/Compat.cpp
    SpioCore/Paths.cpp
    SpioCore/Process.cpp
    SpioCore/Sha256.cpp
)

set(SPIO_MANIFEST_SOURCES
    SpioManifest/Lockfile.cpp
    SpioManifest/Manifest.cpp
)

set(SPIO_RESOLUTION_SOURCES
    SpioRegistryClient/Client.cpp
    SpioResolve/Resolver.cpp
    SpioSecurity/RegistrySecurity.cpp
)

set(SPIO_TOOLCHAIN_SERVICE_SOURCES
    SpioCloud/Contract.cpp
    SpioCloud/Execution.cpp
    SpioCloud/Job.cpp
    SpioPlan/CompilePlan.cpp
    SpioTool/Contract.cpp
    SpioTool/Install.cpp
    SpioToolchain/SourceBuild.cpp
    SpioToolchain/State.cpp
)

set(SPIO_PACKAGE_SERVICE_SOURCES
    SpioPack/Pack.cpp
    SpioPublish/Publish.cpp
    SpioTree/Render.cpp
    SpioVendor/Vendor.cpp
    SpioWorkflow/Dependencies.cpp
)

set(SPIO_PROJECT_SERVICE_SOURCES
    SpioResolve/ProjectGraphContract.cpp
)

set(SPIO_CLI_SUPPORT_SOURCES
    SpioCLI/MachineInfoContract.cpp
    SpioCLI/Support.cpp
)

set(SPIO_CLI_COMMAND_SOURCES
    SpioApp/CloudApp.cpp
    SpioApp/PackageApp.cpp
    SpioApp/ToolApp.cpp
    SpioApp/WorkflowApp.cpp
)

set(SPIO_CLI_SHELL_SOURCES
    SpioCLI/CLI.cpp
)

file(GLOB_RECURSE SPIO_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src-private/*.cpp")

spio_add_internal_static_library(spio_foundation ${SPIO_FOUNDATION_SOURCES})

spio_add_internal_static_library(spio_manifest ${SPIO_MANIFEST_SOURCES})
target_link_libraries(spio_manifest PUBLIC spio_foundation)

spio_add_internal_static_library(spio_resolution ${SPIO_RESOLUTION_SOURCES})
target_link_libraries(spio_resolution PUBLIC spio_foundation spio_manifest)

spio_add_internal_static_library(spio_toolchain_service ${SPIO_TOOLCHAIN_SERVICE_SOURCES})
target_link_libraries(spio_toolchain_service PUBLIC spio_foundation spio_manifest spio_resolution)

spio_add_internal_static_library(spio_package_service ${SPIO_PACKAGE_SERVICE_SOURCES})
target_link_libraries(spio_package_service PUBLIC spio_foundation spio_manifest spio_resolution)

spio_add_internal_static_library(spio_project_service ${SPIO_PROJECT_SERVICE_SOURCES})
target_link_libraries(spio_project_service PUBLIC spio_foundation spio_manifest spio_resolution spio_toolchain_service)

add_library(spio_backend_services INTERFACE)
target_link_libraries(spio_backend_services INTERFACE
  spio_foundation
  spio_manifest
  spio_resolution
  spio_toolchain_service
  spio_package_service
  spio_project_service
)

spio_add_internal_static_library(spio_cli_support ${SPIO_CLI_SUPPORT_SOURCES})
target_link_libraries(spio_cli_support PUBLIC spio_foundation spio_manifest spio_resolution spio_toolchain_service)

spio_add_internal_static_library(spio_cli_commands ${SPIO_CLI_COMMAND_SOURCES})
target_link_libraries(spio_cli_commands PUBLIC spio_backend_services spio_cli_support)

spio_add_internal_static_library(spio_cli_shell ${SPIO_CLI_SHELL_SOURCES})
target_link_libraries(spio_cli_shell PUBLIC spio_cli_commands spio_cli_support)

if(SPIO_PRIVATE_SOURCES)
  spio_add_internal_static_library(spio_private_extensions ${SPIO_PRIVATE_SOURCES})
  target_link_libraries(spio_private_extensions PUBLIC spio_backend_services spio_cli_support)
  target_link_libraries(spio_cli_shell PUBLIC spio_private_extensions)
endif()

# Transitional compatibility aggregation for tests and internal link points.
add_library(spio_core INTERFACE)
target_link_libraries(spio_core INTERFACE spio_cli_shell)
if(TARGET spio_private_extensions)
  target_link_libraries(spio_core INTERFACE spio_private_extensions)
endif()

add_executable(spio main.cpp)
target_link_libraries(spio PRIVATE spio_cli_shell)
