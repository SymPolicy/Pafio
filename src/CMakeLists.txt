add_library(pafio_build_config INTERFACE)
target_include_directories(pafio_build_config INTERFACE "${CMAKE_SOURCE_DIR}/src")
set(PAFIO_VERSION_SUFFIX "-dev" CACHE STRING "Suffix appended to the pafio project version, for example -dev or -alpha.")
target_compile_definitions(pafio_build_config INTERFACE
  "PAFIO_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
  "PAFIO_PROJECT_VERSION=\"${PROJECT_VERSION}${PAFIO_VERSION_SUFFIX}\""
)
target_link_libraries(pafio_build_config INTERFACE tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json)

function(pafio_add_internal_static_library target)
  add_library(${target} STATIC ${ARGN})
  target_link_libraries(${target} PUBLIC pafio_build_config)
endfunction()

set(PAFIO_FOUNDATION_SOURCES
    PafioCompat/Compat.cpp
    PafioCore/AtomicFile.cpp
    PafioCore/ContractLexicon.cpp
    PafioCore/FileLock.cpp
    PafioCore/Paths.cpp
    PafioCore/Process.cpp
    PafioCore/Sha256.cpp
    PafioCore/ToolPaths.cpp
)

if(WIN32)
  list(APPEND PAFIO_FOUNDATION_SOURCES PafioCore/ProcessWindows.cpp)
else()
  list(APPEND PAFIO_FOUNDATION_SOURCES PafioCore/ProcessPosix.cpp)
endif()

set(PAFIO_MANIFEST_SOURCES
    PafioManifest/Lockfile.cpp
    PafioManifest/Manifest.cpp
)

set(PAFIO_RESOLUTION_SOURCES
    PafioRegistryClient/Client.cpp
    PafioResolve/MetadataContract.cpp
    PafioResolve/ResolutionContract.cpp
    PafioResolve/Resolver.cpp
    PafioSecurity/Ed25519.cpp
    PafioSecurity/PrescanArchive.cpp
    PafioSecurity/RegistrySecurity.cpp
    PafioSecurity/RegistryTrust.cpp
    PafioSecurity/TufVerifier.cpp
)

set(PAFIO_PLAN_SOURCES
    PafioPlan/CompilePlan.cpp
)

set(PAFIO_PACKAGE_SERVICE_SOURCES
    PafioPack/Pack.cpp
    PafioPack/PackageSelection.cpp
    PafioPublish/Publish.cpp
    PafioTree/Render.cpp
    PafioVendor/Vendor.cpp
    PafioWorkflow/Dependencies.cpp
)

set(PAFIO_CLI_SUPPORT_SOURCES
    PafioCLI/MachineInfoContract.cpp
    PafioCLI/Support.cpp
)

set(PAFIO_CLI_COMMAND_SOURCES
    PafioApp/DoctorApp.cpp
    PafioApp/PackageApp.cpp
    PafioApp/WorkflowApp.cpp
)

set(PAFIO_CLI_SHELL_SOURCES
    PafioCLI/CLI.cpp
)

set(PAFIO_COMPAT_CONTRACT "${CMAKE_SOURCE_DIR}/contracts/compat/styio-support.toml")
file(READ "${PAFIO_COMPAT_CONTRACT}" PAFIO_STYIO_SUPPORT_TOML)
set(PAFIO_GENERATED_INCLUDE "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PAFIO_GENERATED_INCLUDE}")
file(WRITE "${PAFIO_GENERATED_INCLUDE}/EmbeddedStyioSupportToml.inc"
  "R\"PAFIO_TOML(\n${PAFIO_STYIO_SUPPORT_TOML})PAFIO_TOML\"")
set_source_files_properties(PafioCompat/Compat.cpp PROPERTIES
  OBJECT_DEPENDS "${PAFIO_GENERATED_INCLUDE}/EmbeddedStyioSupportToml.inc")

pafio_add_internal_static_library(pafio_foundation ${PAFIO_FOUNDATION_SOURCES})
target_include_directories(pafio_foundation PRIVATE "${PAFIO_GENERATED_INCLUDE}")
if(WIN32)
  target_link_libraries(pafio_foundation PRIVATE Rpcrt4)
endif()

pafio_add_internal_static_library(pafio_manifest ${PAFIO_MANIFEST_SOURCES})
target_link_libraries(pafio_manifest PUBLIC pafio_foundation)

pafio_add_internal_static_library(pafio_resolution ${PAFIO_RESOLUTION_SOURCES})
target_link_libraries(pafio_resolution PUBLIC pafio_foundation pafio_manifest)

pafio_add_internal_static_library(pafio_plan ${PAFIO_PLAN_SOURCES})
target_link_libraries(pafio_plan PUBLIC pafio_foundation pafio_manifest pafio_resolution)

pafio_add_internal_static_library(pafio_package_service ${PAFIO_PACKAGE_SERVICE_SOURCES})
target_link_libraries(pafio_package_service PUBLIC pafio_foundation pafio_manifest pafio_resolution)

add_library(pafio_backend_services INTERFACE)
target_link_libraries(pafio_backend_services INTERFACE
  pafio_foundation
  pafio_manifest
  pafio_resolution
  pafio_plan
  pafio_package_service
)

pafio_add_internal_static_library(pafio_cli_support ${PAFIO_CLI_SUPPORT_SOURCES})
target_link_libraries(pafio_cli_support PUBLIC pafio_foundation pafio_manifest pafio_resolution pafio_plan)

pafio_add_internal_static_library(pafio_cli_commands ${PAFIO_CLI_COMMAND_SOURCES})
target_link_libraries(pafio_cli_commands PUBLIC pafio_backend_services pafio_cli_support)

pafio_add_internal_static_library(pafio_cli_shell ${PAFIO_CLI_SHELL_SOURCES})
target_link_libraries(pafio_cli_shell PUBLIC pafio_cli_commands pafio_cli_support)

add_executable(pafio main.cpp)
target_link_libraries(pafio PRIVATE pafio_cli_shell)
