#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

#include "BuildTestSupport.hpp"
#include "PafioCompat/Compat.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace
{

std::filesystem::path CurrentExecutablePath()
{
  std::wstring value(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
  if (length == 0 || length >= value.size())
  {
    return {};
  }
  value.resize(length);
  return std::filesystem::path(value);
}

TEST(WindowsProcessProducerTests, CompilerProbeRunsFromUnicodeFilesystemPath)
{
  namespace fs = std::filesystem;
  const fs::path test_executable = CurrentExecutablePath();
  ASSERT_TRUE(!test_executable.empty());

  const fs::path fixture_source = test_executable.parent_path() / L"pafio_process_tests.exe";
  ASSERT_TRUE(fs::is_regular_file(fixture_source));

  const fs::path root = pafio::testsupport::MakeTempDir("windows-compiler-probe-path");
  const fs::path unicode_directory = root / fs::path(std::u8string(u8"compiler-\u4e2d\u6587"));
  std::error_code error;
  fs::create_directories(unicode_directory, error);
  ASSERT_FALSE(error);

  const fs::path fixture = unicode_directory / L"styio.exe";
  fs::copy_file(fixture_source, fixture, fs::copy_options::overwrite_existing, error);
  ASSERT_FALSE(error);

  bool compatible = false;
  try
  {
    const pafio::CompatibilityReport report = pafio::CheckCompilerCompatibility(fixture);
    compatible = report.compiler_version == "0.0.5";
  }
  catch (...)
  {
    // Keep filesystem and process errors from emitting host directory values.
  }
  EXPECT_TRUE(compatible);

  fs::remove_all(root, error);
}

}  // namespace
#endif
