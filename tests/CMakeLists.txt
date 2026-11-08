include(FetchContent)
include(GoogleTest)

if(MSVC)
  set(gtest_force_shared_crt ON CACHE BOOL "Match the product's MSVC runtime" FORCE)
endif()

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  URL_HASH SHA256=edd885a1ab32b6999515a880f669efadb80b3f880215f315985fa3f6eca7c4d3
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

FetchContent_MakeAvailable(googletest)

add_executable(pafio_process_tests native/ProcessPortableTests.cpp)
target_link_libraries(pafio_process_tests PRIVATE pafio_foundation GTest::gtest)
if(MINGW)
  target_link_options(pafio_process_tests PRIVATE -municode)
endif()
gtest_discover_tests(pafio_process_tests DISCOVERY_MODE PRE_TEST)

if(WIN32)
  add_executable(pafio_windows_process_producer_tests native/WindowsProcessProducerTests.cpp)
  target_link_libraries(pafio_windows_process_producer_tests PRIVATE pafio_cli_shell GTest::gtest_main)
  add_dependencies(pafio_windows_process_producer_tests pafio_process_tests)
  gtest_discover_tests(pafio_windows_process_producer_tests DISCOVERY_TIMEOUT 30)
endif()

add_executable(pafio_native_tests
  native/BuildPlanTests.cpp
  native/BuildWorkflowCliTests.cpp
  native/DoctorTests.cpp
  native/LockTests.cpp
  native/ManifestTests.cpp
  native/MetadataTests.cpp
  native/PackTests.cpp
  native/PathsTests.cpp
  native/ProcessTests.cpp
  native/PublishTests.cpp
  native/ResolutionContractTests.cpp
  native/RunAndTestCliTests.cpp
  native/SecurityTests.cpp
  native/SyncTests.cpp
  native/TufTests.cpp
  native/VendorTests.cpp
)
target_link_libraries(pafio_native_tests PRIVATE pafio_cli_shell GTest::gtest_main)
add_dependencies(pafio_native_tests pafio_process_tests)

gtest_discover_tests(pafio_native_tests DISCOVERY_TIMEOUT 30)

set(PAFIO_TEST_STYIO_BIN "" CACHE FILEPATH "Explicit real Styio candidate for workflow integration")
if(PAFIO_TEST_STYIO_BIN)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME pafio_task_resource_real_workflow
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/styio-interface-gate.py"
      --pafio-bin "$<TARGET_FILE:pafio>" --styio-bin "${PAFIO_TEST_STYIO_BIN}"
      --require-compile-plan --task-resource
      --work-dir "${CMAKE_BINARY_DIR}/task-resource-ctest" --json
  )
  set_tests_properties(pafio_task_resource_real_workflow PROPERTIES LABELS "integration;real-styio")
endif()

add_test(
  NAME pafio_cli_machine_info_json
  COMMAND bash -c "out=$(\n\"$<TARGET_FILE:pafio>\" machine-info --json\n) && printf '%s\n' \"$out\" | grep -q '\"implementation_language\":\"c++\"' && printf '%s\n' \"$out\" | grep -q '\"metadata\":\\[1\\]' && printf '%s\n' \"$out\" | grep -q '\"resolution\":\\[1\\]' && ! printf '%s\n' \"$out\" | grep -q '\"bootstrap\"' && ! printf '%s\n' \"$out\" | grep -q 'cloud_execution_policy'"
)

add_test(
  NAME pafio_cli_version
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" --version | grep -q '^pafio '"
)

add_test(
  NAME pafio_cli_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" --help | grep -q '^pafio usage:'"
)

add_test(
  NAME pafio_cli_short_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" -h | grep -q '^pafio usage:'"
)

add_test(
  NAME pafio_cli_machine_info_rejects_unknown_flag
  COMMAND bash -c "out_file=\$(mktemp) && err_file=\$(mktemp) && trap 'rm -f \"\$out_file\" \"\$err_file\"' EXIT && ! \"$<TARGET_FILE:pafio>\" machine-info --bogus >\"\$out_file\" 2>\"\$err_file\" && grep -q 'machine-info accepts only --json' \"\$err_file\""
)

add_test(
  NAME pafio_cli_doctor_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" doctor --help | grep -q '^usage: pafio doctor \\[--json\\] \\[--manifest-path <path>\\] \\[--styio-bin <path>\\]$'"
)

add_test(
  NAME pafio_cli_metadata_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" metadata --help | grep -q '^usage: pafio metadata --json \\[--manifest-path <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME pafio_cli_tree_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" tree --help | grep -q '^usage: pafio tree \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME pafio_cli_add_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" add --help | grep -q '^usage: pafio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) \\[--alias <name>\\] \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME pafio_cli_remove_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" remove --help | grep -q '^usage: pafio remove <alias-or-package> \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME pafio_cli_sync_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" sync --help | grep -q '^usage: pafio sync \\[--manifest-path <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME pafio_cli_check_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" check --help | grep -Fqx 'usage: pafio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]'"
)

add_test(
  NAME pafio_cli_build_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" build --help | grep -Fqx 'usage: pafio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]'"
)

add_test(
  NAME pafio_cli_build_rejects_positional_mode
  COMMAND bash -c "out_file=\$(mktemp) && err_file=\$(mktemp) && trap 'rm -f \"\$out_file\" \"\$err_file\"' EXIT && ! \"$<TARGET_FILE:pafio>\" build minimal >\"\$out_file\" 2>\"\$err_file\" && grep -q 'unexpected argument for build: minimal' \"\$err_file\""
)

add_test(
  NAME pafio_cli_run_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" run --help | grep -Fqx 'usage: pafio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]'"
)

add_test(
  NAME pafio_cli_test_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" test --help | grep -Fqx 'usage: pafio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen] [--emit-observable-static-snapshot[=<schema-version>]] [--observable-capability <name>] [--observable-parent-snapshot <path>] [--emit-runtime-observation[=<version>]] [--runtime-observation-mode <disabled|aggregate|sampled|detailed>] [--runtime-observation-capability <name>] [--runtime-observation-lane-capacity <n>] [--runtime-observation-sampling <numerator>/<denominator>[@<seed>]]'"
)

add_test(
  NAME pafio_cli_vendor_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" vendor --help | grep -q '^usage: pafio vendor \\[--manifest-path <path>\\] \\[--output <path>\\] \\[--locked\\|--offline\\|--frozen\\]$'"
)

add_test(
  NAME pafio_cli_pack_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" pack --help | grep -q '^usage: pafio pack \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--output <path>\\]$'"
)

add_test(
  NAME pafio_cli_publish_help
  COMMAND bash -c "\"$<TARGET_FILE:pafio>\" publish --help | grep -Fqx 'usage: pafio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <http(s)-url>] [--dry-run] [--allow-plaintext-http]'"
)
