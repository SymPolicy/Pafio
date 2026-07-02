add_library(pafio_build_config INTERFACE)
target_include_directories(pafio_build_config INTERFACE "${CMAKE_SOURCE_DIR}/src")
if(EXISTS "${CMAKE_SOURCE_DIR}/src-private")
  target_include_directories(pafio_build_config INTERFACE "${CMAKE_SOURCE_DIR}/src-private")
endif()
target_compile_definitions(pafio_build_config INTERFACE
  "PAFIO_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
  "PAFIO_PROJECT_VERSION=\"${PROJECT_VERSION}-dev\""
)
target_link_libraries(pafio_build_config INTERFACE tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

function(pafio_add_internal_static_library target)
  add_library(${target} STATIC ${ARGN})
  target_link_libraries(${target} PUBLIC pafio_build_config)
endfunction()

set(PAFIO_FOUNDATION_SOURCES
    PafioCompat/Compat.cpp
    PafioCore/Paths.cpp
    PafioCore/Process.cpp
    PafioCore/Sha256.cpp
)

set(PAFIO_MANIFEST_SOURCES
    PafioManifest/Lockfile.cpp
    PafioManifest/Manifest.cpp
)

set(PAFIO_RESOLUTION_SOURCES
    PafioRegistryClient/Client.cpp
    PafioResolve/Resolver.cpp
    PafioSecurity/RegistrySecurity.cpp
    PafioSecurity/RegistryTrust.cpp
)

set(PAFIO_TOOLCHAIN_SERVICE_SOURCES
    PafioCloud/Contract.cpp
    PafioCloud/Execution.cpp
    PafioCloud/Job.cpp
    PafioPlan/CompilePlan.cpp
    PafioTool/Contract.cpp
    PafioTool/Install.cpp
    PafioToolchain/SourceBuild.cpp
    PafioToolchain/State.cpp
)

set(PAFIO_PACKAGE_SERVICE_SOURCES
    PafioPack/Pack.cpp
    PafioPublish/Publish.cpp
    PafioTree/Render.cpp
    PafioVendor/Vendor.cpp
    PafioWorkflow/Dependencies.cpp
)

set(PAFIO_PROJECT_SERVICE_SOURCES
    PafioResolve/ProjectGraphContract.cpp
)

set(PAFIO_CLI_SUPPORT_SOURCES
    PafioCLI/MachineInfoContract.cpp
    PafioCLI/Support.cpp
)

set(PAFIO_CLI_COMMAND_SOURCES
    PafioApp/CloudApp.cpp
    PafioApp/PackageApp.cpp
    PafioApp/ToolApp.cpp
    PafioApp/WorkflowApp.cpp
)

set(PAFIO_CLI_SHELL_SOURCES
    PafioCLI/CLI.cpp
)

file(GLOB_RECURSE PAFIO_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src-private/*.cpp")

pafio_add_internal_static_library(pafio_foundation ${PAFIO_FOUNDATION_SOURCES})

pafio_add_internal_static_library(pafio_manifest ${PAFIO_MANIFEST_SOURCES})
target_link_libraries(pafio_manifest PUBLIC pafio_foundation)

pafio_add_internal_static_library(pafio_resolution ${PAFIO_RESOLUTION_SOURCES})
target_link_libraries(pafio_resolution PUBLIC pafio_foundation pafio_manifest)

pafio_add_internal_static_library(pafio_toolchain_service ${PAFIO_TOOLCHAIN_SERVICE_SOURCES})
target_link_libraries(pafio_toolchain_service PUBLIC pafio_foundation pafio_manifest pafio_resolution)

pafio_add_internal_static_library(pafio_package_service ${PAFIO_PACKAGE_SERVICE_SOURCES})
target_link_libraries(pafio_package_service PUBLIC pafio_foundation pafio_manifest pafio_resolution)

pafio_add_internal_static_library(pafio_project_service ${PAFIO_PROJECT_SERVICE_SOURCES})
target_link_libraries(pafio_project_service PUBLIC pafio_foundation pafio_manifest pafio_resolution pafio_toolchain_service)

add_library(pafio_backend_services INTERFACE)
target_link_libraries(pafio_backend_services INTERFACE
  pafio_foundation
  pafio_manifest
  pafio_resolution
  pafio_toolchain_service
  pafio_package_service
  pafio_project_service
)

pafio_add_internal_static_library(pafio_cli_support ${PAFIO_CLI_SUPPORT_SOURCES})
target_link_libraries(pafio_cli_support PUBLIC pafio_foundation pafio_manifest pafio_resolution pafio_toolchain_service)

pafio_add_internal_static_library(pafio_cli_commands ${PAFIO_CLI_COMMAND_SOURCES})
target_link_libraries(pafio_cli_commands PUBLIC pafio_backend_services pafio_cli_support)

pafio_add_internal_static_library(pafio_cli_shell ${PAFIO_CLI_SHELL_SOURCES})
target_link_libraries(pafio_cli_shell PUBLIC pafio_cli_commands pafio_cli_support)

if(PAFIO_PRIVATE_SOURCES)
  pafio_add_internal_static_library(pafio_private_extensions ${PAFIO_PRIVATE_SOURCES})
  target_link_libraries(pafio_private_extensions PUBLIC pafio_backend_services pafio_cli_support)
  target_link_libraries(pafio_cli_shell PUBLIC pafio_private_extensions)
endif()

# Transitional compatibility aggregation for tests and internal link points.
add_library(pafio_core INTERFACE)
target_link_libraries(pafio_core INTERFACE pafio_cli_shell)
if(TARGET pafio_private_extensions)
  target_link_libraries(pafio_core INTERFACE pafio_private_extensions)
endif()

add_executable(pafio main.cpp)
target_link_libraries(pafio PRIVATE pafio_cli_shell)
