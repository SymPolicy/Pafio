include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

FetchContent_MakeAvailable(googletest)

add_executable(spio_native_tests
  native/BuildCloudTests.cpp
  native/BuildPlanTests.cpp
  native/BuildWorkflowCliTests.cpp
  native/LockTests.cpp
  native/ManifestTests.cpp
  native/PackTests.cpp
  native/ProcessTests.cpp
  native/PublishTests.cpp
  native/RunAndTestCliTests.cpp
  native/SecurityTests.cpp
  native/SyncTests.cpp
  native/ToolInstallTests.cpp
  native/ToolPinTests.cpp
  native/ToolchainStateTests.cpp
  native/ToolStatusTests.cpp
  native/VendorTests.cpp
)
target_link_libraries(spio_native_tests PRIVATE spio_core GTest::gtest_main)

gtest_discover_tests(spio_native_tests DISCOVERY_TIMEOUT 30)

add_test(
  NAME spio_cli_machine_info_json
  COMMAND bash -c "out=$(\n\"$<TARGET_FILE:spio>\" machine-info --json\n) && printf '%s\n' \"$out\" | grep -q '\"implementation_language\":\"c++\"' && printf '%s\n' \"$out\" | grep -q '\"cloud_execution_policy\":\\[1\\]' && printf '%s\n' \"$out\" | grep -q '\"build_job_request\":\\[1\\]'"
)

add_test(
  NAME spio_cli_version
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" --version | grep -q '^spio '"
)

add_test(
  NAME spio_cli_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" --help | grep -q '^spio usage:'"
)

add_test(
  NAME spio_cli_machine_info_rejects_unknown_flag
  COMMAND bash -c "out_file=\$(mktemp) && err_file=\$(mktemp) && trap 'rm -f \"\$out_file\" \"\$err_file\"' EXIT && ! \"$<TARGET_FILE:spio>\" machine-info --bogus >\"\$out_file\" 2>\"\$err_file\" && grep -q 'machine-info accepts only --json' \"\$err_file\""
)

add_test(
  NAME spio_cli_project_graph_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" project-graph --help | grep -q '^usage: spio project-graph --json \\[--manifest-path <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_lock_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" lock --help | grep -q '^usage: spio lock \\[--manifest-path <path>\\] \\[--check\\] \\[--offline\\]$'"
)

add_test(
  NAME spio_cli_tree_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" tree --help | grep -q '^usage: spio tree \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_add_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" add --help | grep -q '^usage: spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) \\[--alias <name>\\] \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_remove_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" remove --help | grep -q '^usage: spio remove <alias-or-package> \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_fetch_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" fetch --help | grep -q '^usage: spio fetch \\[--manifest-path <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_sync_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" sync --help | grep -q '^usage: spio sync \\[--manifest-path <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_build_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" build --help | grep -q '^usage: spio build \\[minimal\\] \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--bin <name>\\|--lib\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\] \\[--source-root <path>\\] \\[--source-rev <rev>\\] \\[--yes\\|--no-fetch\\|--non-interactive\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_run_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" run --help | grep -q '^usage: spio run \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--bin <name>\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_test_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" test --help | grep -q '^usage: spio test \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--test <name>\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_use_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" use --help | grep -q '^usage: spio use <binary\\|build> \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_install_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" install --help | grep -q '^usage: spio install styio\\[@latest\\] \\[--source-root <path>\\] \\[--source-rev <ref>\\] \\[--channel <stable\\|nightly>\\] \\[--build <minimal>\\] \\[--yes\\|--no-fetch\\|--offline\\|--non-interactive\\]$'"
)

add_test(
  NAME spio_installer_bootstrap_smoke
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/spio-installer-bootstrap-smoke.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_cli_set_help
  COMMAND bash -c "out=$(\n\"$<TARGET_FILE:spio>\" set --help\n) && printf '%s\n' \"$out\" | grep -q '^  spio set channel as <stable\\|nightly> \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio set build as <minimal> \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio set risk as <trusted-internal\\|partner-controlled\\|untrusted-user> \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio set lane as <isolated\\|warm-shared> \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio set security as <sandbox-default\\|partner-restricted\\|trusted-warm> \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_cloud_help
  COMMAND bash -c "out=$(\n\"$<TARGET_FILE:spio>\" cloud --help\n) && printf '%s\n' \"$out\" | grep -q '^usage: spio cloud <status|plan> ...$' && printf '%s\n' \"$out\" | grep -q '^  spio cloud status \\[--json\\] \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio cloud plan --json <build|run|test> \\[minimal\\] \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--bin <name>|--lib|--test <name>\\] \\[--profile <dev|release>\\] \\[--source-root <path>\\] \\[--source-rev <rev>\\] \\[--yes|--no-fetch|--non-interactive\\] \\[--locked|--offline|--frozen\\]$'"
)

add_test(
  NAME spio_cli_vendor_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" vendor --help | grep -q '^usage: spio vendor \\[--manifest-path <path>\\] \\[--output <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME spio_cli_pack_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" pack --help | grep -q '^usage: spio pack \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--output <path>\\]$'"
)

add_test(
  NAME spio_cli_publish_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" publish --help | grep -q '^usage: spio publish \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--output <path>\\] \\[--registry <path-or-url>\\] \\[--registry-profile <name>\\] \\[--registry-policy-file <path>\\] \\[--registry-header <name:value>\\] \\[--dry-run\\]$'"
)

add_test(
  NAME spio_cli_tool_install_help
  COMMAND bash -c "out=$(\n\"$<TARGET_FILE:spio>\" tool --help\n) && printf '%s\n' \"$out\" | grep -q '^  spio tool install --styio-bin <path>$' && printf '%s\n' \"$out\" | grep -q '^  spio tool status --json \\[--manifest-path <path>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio tool use --version <compiler-version> \\[--channel <channel>\\]$' && printf '%s\n' \"$out\" | grep -q '^  spio tool pin (--version <compiler-version> \\[--channel <channel>\\] | --clear) \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_tool_pin_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" tool pin --help | grep -q '^usage: spio tool pin (--version <compiler-version> \\[--channel <channel>\\] | --clear) \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_styio_interface_gate_handshake
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/styio-interface-gate-handshake.sh"
)

add_test(
  NAME spio_styio_interface_gate_compile_plan
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/styio-interface-gate-compile-plan.sh"
)

add_test(
  NAME spio_registry_http_fetch
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-http-fetch.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_http_publish
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-http-publish.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_server_gate
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-server-gate.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_split_origin_promotion
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-split-origin-promotion.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_split_origin_http
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-split-origin-http.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_v2_unit
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/unit/test_registry_v2.py"
)

add_test(
  NAME spio_cloud_compile_stress_unit
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/unit/test_cloud_compile_stress.py"
)

add_test(
  NAME spio_registry_v2_contract
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/registry-v2-contract-gate.py"
)

add_test(
  NAME spio_registry_v2_publish
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-v2-publish.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_v2_http_read
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-v2-http-read.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_registry_control_plane_contract
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/registry-control-plane-contract-gate.py"
)

add_test(
  NAME spio_registry_v2_control_plane_http
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/interop/registry-v2-control-plane-http.sh" "$<TARGET_FILE:spio>"
)

add_test(
  NAME spio_native_contract_source_gate
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/native-contract-source-gate.py"
)

add_test(
  NAME spio_hosted_api_contract_unit
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/hosted-control-plane-contract-gate.py" --mode unit
)

add_test(
  NAME spio_hosted_api_contract_integration
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/hosted-control-plane-contract-gate.py" --mode integration
)

add_test(
  NAME spio_hosted_api_contract_regression
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/hosted-control-plane-contract-gate.py" --mode regression
)

add_test(
  NAME spio_hosted_api_contract_smoke
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/hosted-control-plane-contract-gate.py" --mode smoke
)

add_test(
  NAME spio_hosted_api_contract_fuzz
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/interop/hosted-control-plane-contract-gate.py" --mode fuzz
)

if(EXISTS "${CMAKE_SOURCE_DIR}/tests-private/CMakeLists.txt")
  add_subdirectory("${CMAKE_SOURCE_DIR}/tests-private" "${CMAKE_BINARY_DIR}/tests-private")
endif()
