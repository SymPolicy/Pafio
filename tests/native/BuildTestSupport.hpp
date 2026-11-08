#pragma once

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
  }

  ~ScopedEnvVar()
  {
    if (had_previous_)
    {
#if defined(_WIN32)
      _putenv_s(name_.c_str(), previous_value_.c_str());
#else
      setenv(name_.c_str(), previous_value_.c_str(), 1);
#endif
    }
    else
    {
#if defined(_WIN32)
      _putenv_s(name_.c_str(), "");
#else
      unsetenv(name_.c_str());
#endif
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

#if !defined(_WIN32)
struct ChildProcessResult
{
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

inline ChildProcessResult RunChildProcess(const std::string &binary, const std::vector<std::string> &args)
{
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    throw std::runtime_error("failed to create test pipes");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    throw std::runtime_error("failed to fork test process");
  }

  if (child == 0)
  {
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(binary.c_str(), argv.data());
    _exit(127);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  auto read_all = [](int fd) {
    std::string text;
    std::array<char, 4096> buffer{};
    ssize_t read_size = 0;
    while ((read_size = read(fd, buffer.data(), buffer.size())) > 0)
    {
      text.append(buffer.data(), static_cast<size_t>(read_size));
    }
    close(fd);
    return text;
  };

  ChildProcessResult result;
  result.stdout_text = read_all(stdout_pipe[0]);
  result.stderr_text = read_all(stderr_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  return result;
}

inline std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
}

inline void RunGitOrAssert(const std::vector<std::string> &args)
{
  const ChildProcessResult result = RunChildProcess("git", args);
  ASSERT_EQ(result.exit_code, 0) << TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
}

inline std::string GitHeadRev(const fs::path &repo_root)
{
  const ChildProcessResult result = RunChildProcess("git", {"-C", repo_root.string(), "rev-parse", "HEAD"});
  EXPECT_EQ(result.exit_code, 0) << TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
  return TrimTrailingNewline(result.stdout_text);
}

inline std::string CreateWorkspaceGitRepo(const fs::path &repo_root, const std::string &util_version)
{
  fs::create_directories(repo_root);
  RunGitOrAssert({"init", "--initial-branch=main", repo_root.string()});

  WriteFile(
      repo_root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[workspace]\n"
      "members = [\"packages/feed\", \"packages/util\"]\n"
      "resolver = \"1\"\n");
  WriteFile(
      repo_root / "packages/feed/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/feed\"\n"
      "version = \"1.2.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"../util\" }\n");
  WriteFile(
      repo_root / "packages/util/pafio.toml",
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/util\"\n"
          "version = \"") +
          util_version +
          "\"\n"
          "edition = \"2026\"\n\n"
          "[build]\n"
          "implicit-std = true\n\n"
          "[lib]\n"
          "path = \"src/lib.styio\"\n");

  RunGitOrAssert(
      {"-C", repo_root.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "add", "."});
  RunGitOrAssert(
      {"-C", repo_root.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "commit", "--quiet", "-m", "initial"});
  return GitHeadRev(repo_root);
}
#endif  // !defined(_WIN32)

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
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"nightly\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"supported_contracts\":{\"compile_plan\":[1]},\"supported_contract_versions\":{\"machine_info\":[1],\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n" +
          std::string(
              "if [ \"$1\" = \"--version\" ]; then\n"
              "  printf '%s\\n' 'styio 0.0.5'\n"
              "  exit 0\n"
              "fi\n") +
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
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"--version\" ]; then\n"
      "  printf '%s\\n' 'styio 0.0.5'\n"
      "  exit 0\n"
      "fi\n"
      + FakeCompilePlanConsumerBody() +
          "echo unexpected invocation >&2\n"
          "exit 64\n");
}

}  // namespace pafio::testsupport
