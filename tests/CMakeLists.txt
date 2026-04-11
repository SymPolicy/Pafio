include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

FetchContent_MakeAvailable(googletest)

add_executable(spio_native_tests
  native/BuildTests.cpp
  native/LockTests.cpp
  native/ManifestTests.cpp
  native/PackTests.cpp
)
target_link_libraries(spio_native_tests PRIVATE spio_core GTest::gtest_main)

gtest_discover_tests(spio_native_tests DISCOVERY_TIMEOUT 30)

add_test(
  NAME spio_cli_machine_info_json
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" machine-info --json | grep -q '\"implementation_language\":\"c++\"'"
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
  NAME spio_cli_lock_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" lock --help | grep -q '^usage: spio lock \\[--manifest-path <path>\\] \\[--check\\]$'"
)

add_test(
  NAME spio_cli_tree_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" tree --help | grep -q '^usage: spio tree \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_add_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" add --help | grep -q '^usage: spio add <package-name> (--path <path> | --git <source> --rev <rev>) \\[--alias <name>\\] \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_remove_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" remove --help | grep -q '^usage: spio remove <alias-or-package> \\[--dev\\] \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_fetch_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" fetch --help | grep -q '^usage: spio fetch \\[--manifest-path <path>\\]$'"
)

add_test(
  NAME spio_cli_build_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" build --help | grep -q '^usage: spio build \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--bin <name>\\|--lib\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\]$'"
)

add_test(
  NAME spio_cli_run_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" run --help | grep -q '^usage: spio run \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--bin <name>\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\]$'"
)

add_test(
  NAME spio_cli_test_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" test --help | grep -q '^usage: spio test \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--test <name>\\] \\[--profile <dev\\|release>\\] \\[--dry-run\\] \\[--styio-bin <path>\\]$'"
)

add_test(
  NAME spio_cli_pack_help
  COMMAND bash -c "\"$<TARGET_FILE:spio>\" pack --help | grep -q '^usage: spio pack \\[--manifest-path <path>\\] \\[--package <package-name>\\] \\[--output <path>\\]$'"
)
