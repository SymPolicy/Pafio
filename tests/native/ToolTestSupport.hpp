#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace spio_test_support
{

class ScopedEnvVar
{
public:
  ScopedEnvVar(const std::string &name, const std::string &value)
      : name_(name)
  {
    if (const char *existing = std::getenv(name.c_str()); existing != nullptr)
    {
      had_previous_ = true;
      previous_value_ = existing;
    }
    setenv(name.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar()
  {
    if (had_previous_)
    {
      setenv(name_.c_str(), previous_value_.c_str(), 1);
    }
    else
    {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_previous_ = false;
  std::string previous_value_;
};

inline fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

inline fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "spio-native-tool-tests" / label;
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

inline void WriteFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  ASSERT_TRUE(out.good());
  out << content;
  ASSERT_TRUE(out.good());
}

inline std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

inline void WriteExecutable(const fs::path &path, const std::string &content)
{
  WriteFile(path, content);
  fs::permissions(
      path,
      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
      fs::perm_options::add);
}

inline void WriteFakeStyio(
    const fs::path &path,
    const std::string &version,
    const std::string &channel = "stable",
    const std::string &edition_max = "2026")
{
  WriteExecutable(
      path,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"" +
          version +
          "\",\"channel\":\"" +
          channel +
          "\",\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"" +
          edition_max +
          "\"}'\n"
          "  exit 0\n"
          "fi\n"
          "echo unexpected invocation >&2\n"
          "exit 64\n");
}

inline void WriteSingleBinManifest(
    const fs::path &manifest_path,
    const std::string &package_name = "acme/app")
{
  WriteFile(
      manifest_path,
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"" +
          package_name +
          "\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n"
          "publish = false\n\n"
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"app\"\n"
          "path = \"src/main.styio\"\n");
  WriteFile(manifest_path.parent_path() / "src/main.styio", ">_(\"app\")\n");
}

}  // namespace spio_test_support
