#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace pafio::testsupport
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
  const fs::path root = fs::temp_directory_path() / "pafio-native-build-tests" / label;
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

inline std::string FakeCompilePlanConsumerBody()
{
  return
      "if [ \"$1\" = \"--compile-plan\" ] && [ -n \"${2:-}\" ]; then\n"
      "  python3 - \"$2\" <<'PY'\n"
      "import json, os, pathlib, sys\n"
      "plan = json.load(open(sys.argv[1], 'r', encoding='utf-8'))\n"
      "outputs = plan['outputs']\n"
      "for key in ('build_root', 'artifact_dir', 'diag_dir'):\n"
      "    os.makedirs(outputs[key], exist_ok=True)\n"
      "pathlib.Path(outputs['artifact_dir'], 'fake-artifact.txt').write_text(plan['intent'] + '\\n', encoding='utf-8')\n"
      "pathlib.Path(outputs['diag_dir'], 'diagnostics.jsonl').write_text('', encoding='utf-8')\n"
      "receipt = {\n"
      "    'schema_version': 1,\n"
      "    'tool': 'styio',\n"
      "    'plan_version': plan['plan_version'],\n"
      "    'intent': plan['intent'],\n"
      "    'outputs': outputs,\n"
      "}\n"
      "pathlib.Path(outputs['build_root'], 'receipt.json').write_text(json.dumps(receipt, sort_keys=True) + '\\n', encoding='utf-8')\n"
      "print('fake styio executed compile-plan ' + plan['intent'])\n"
      "PY\n"
      "  exit 0\n"
      "fi\n";
}

inline void WriteFakeCompilePlanStyio(const fs::path &path)
{
  WriteExecutable(
      path,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n" +
          FakeCompilePlanConsumerBody() +
          "echo unexpected invocation >&2\n"
          "exit 64\n");
}

inline void WriteFakeSourceToolchain(const fs::path &root)
{
  WriteFile(
      root / "CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.20)\n"
      "project(fake_styio LANGUAGES NONE)\n"
      "file(MAKE_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\")\n"
      "configure_file(\"${CMAKE_SOURCE_DIR}/styio.sh.in\" \"${CMAKE_BINARY_DIR}/bin/styio\" @ONLY NEWLINE_STYLE UNIX)\n"
      "file(CHMOD \"${CMAKE_BINARY_DIR}/bin/styio\" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)\n"
      "add_custom_target(styio ALL DEPENDS \"${CMAKE_BINARY_DIR}/bin/styio\")\n");
  WriteFile(
      root / "styio.sh.in",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      + FakeCompilePlanConsumerBody() +
          "echo unexpected invocation >&2\n"
          "exit 64\n");
}

}  // namespace pafio::testsupport
