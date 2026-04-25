#include "SpioCLI/CLI.hpp"
#include "SpioCompat/Compat.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioTool/Install.hpp"

#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
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

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "spio-native-tool-tests" / label;
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

void WriteFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  ASSERT_TRUE(out.good());
  out << content;
  ASSERT_TRUE(out.good());
}

std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void WriteExecutable(const fs::path &path, const std::string &content)
{
  WriteFile(path, content);
  fs::permissions(
      path,
      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
      fs::perm_options::add);
}

void WriteFakeStyio(
    const fs::path &path,
    const std::string &version,
    const std::string &channel = "stable",
    const std::string &edition_max = "2026",
    bool compile_plan_consumer = true,
    int stderr_noise_lines = 0)
{
  std::string script =
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"" +
          version +
          "\",\"channel\":\"" +
          channel +
          "\",\"variant\":\"full\",\"active_integration_phase\":\"" +
          std::string(compile_plan_consumer ? "compile-plan-live" : "bootstrap-single-file") +
          "\",\"supported_contracts\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":" +
          std::string(compile_plan_consumer ? "[1]" : "[]") +
          ",\"runtime_events\":[]},\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":" +
          std::string(compile_plan_consumer ? "[1]" : "[]") +
          ",\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":" +
          std::string(compile_plan_consumer ? "true" : "false") +
          ",\"project_execution_via_compile_plan\":" +
          std::string(compile_plan_consumer ? "true" : "false") +
          ",\"runtime_event_stream\":false},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"" +
          edition_max +
          "\"}'\n";
  if (stderr_noise_lines > 0)
  {
    script +=
        "  i=0\n"
        "  while [ \"$i\" -lt " +
        std::to_string(stderr_noise_lines) +
        " ]; do\n"
        "    printf '%s\\n' 'machine-info-noise-machine-info-noise-machine-info-noise-machine-info-noise' >&2\n"
        "    i=$((i + 1))\n"
        "  done\n";
  }
  script +=
      "  exit 0\n"
      "fi\n"
      "echo unexpected invocation >&2\n"
      "exit 64\n";
  WriteExecutable(path, script);
}

void WriteSingleBinManifest(const fs::path &manifest_path, const std::string &package_name = "acme/app")
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

}  // namespace

TEST(ToolInstallTests, InstallsManagedCompilerAndWritesMetadata)
{
  const fs::path root = MakeTempDir("install-managed-compiler");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.0.5");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "install",
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool install");
  EXPECT_EQ(payload.at("compiler_version").get<std::string>(), "0.0.5");

  const fs::path install_binary = payload.at("install_binary_path").get<std::string>();
  const fs::path managed_binary = payload.at("managed_binary_path").get<std::string>();
  const fs::path current_metadata = payload.at("current_metadata_path").get<std::string>();

  EXPECT_TRUE(fs::exists(install_binary));
  EXPECT_TRUE(fs::exists(managed_binary));
  EXPECT_TRUE(fs::exists(current_metadata));

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(managed_binary));

  const json metadata = json::parse(ReadFile(current_metadata));
  EXPECT_EQ(metadata.at("compiler_version").get<std::string>(), "0.0.5");
  EXPECT_EQ(metadata.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(metadata.at("managed_binary").get<std::string>(), CanonicalAbsolutePath(managed_binary).string());
}

TEST(ToolInstallTests, HandlesLargeCompilerProbeStderrWithoutHanging)
{
  const fs::path root = MakeTempDir("tool-install-noisy-probe");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path noisy_styio = root / "styio-noisy";
  WriteFakeStyio(noisy_styio, "0.0.5", "stable", "2026", true, 2048);

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0)
  {
    testing::internal::CaptureStdout();
    const int exit_code = spio::RunCli({
        "--json",
        "tool",
        "install",
        "--styio-bin",
        noisy_styio.string(),
    });
    const std::string stdout_text = testing::internal::GetCapturedStdout();
    if (exit_code != spio::kExitSuccess)
    {
      _exit(20);
    }

    try
    {
      const json payload = json::parse(stdout_text);
      if (payload.at("compiler_version").get<std::string>() != "0.0.5")
      {
        _exit(21);
      }
    }
    catch (const std::exception &)
    {
      _exit(22);
    }
    _exit(0);
  }

  int status = 0;
  bool completed = false;
  for (int attempt = 0; attempt < 50; ++attempt)
  {
    const pid_t waited = waitpid(child, &status, WNOHANG);
    ASSERT_NE(waited, -1);
    if (waited == child)
    {
      completed = true;
      break;
    }
    usleep(100000);
  }

  if (!completed)
  {
    kill(child, SIGKILL);
    (void) waitpid(child, &status, 0);
    FAIL() << "tool install hung while probing compiler stdout/stderr";
  }

  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ToolInstallTests, CheckFallsBackToManagedCompilerWhenNoExplicitPathIsProvided)
{
  const fs::path root = MakeTempDir("check-managed-fallback");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.0.5");

  const spio::ToolInstallResult install = spio::InstallManagedStyio({.styio_binary = fake_styio});

  WriteFile(
      root / "project/spio.toml",
      "[spio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "project/src/main.styio", ">_(\"app\")\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      (root / "project/spio.toml").string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_TRUE(payload.at("compiler_checked").get<bool>());
  EXPECT_EQ(payload.at("styio").at("binary").get<std::string>(), install.managed_binary_path.string());
}

TEST(ToolInstallTests, EnvironmentVariableTakesPrecedenceOverManagedCompiler)
{
  const fs::path root = MakeTempDir("env-precedence");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path installed_styio = root / "installed-styio";
  WriteFakeStyio(installed_styio, "0.0.5");
  (void) spio::InstallManagedStyio({.styio_binary = installed_styio});

  const fs::path explicit_styio = root / "explicit-styio";
  WriteFakeStyio(explicit_styio, "0.0.6");
  const ScopedEnvVar styio_bin("SPIO_STYIO_BIN", explicit_styio.string());

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(explicit_styio));
}

TEST(ToolStatusTests, ReportsManagedStateAndProjectPinResolution)
{
  const fs::path root = MakeTempDir("tool-status-project-pin");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  const spio::ToolInstallResult install_v4 = spio::InstallManagedStyio({.styio_binary = styio_v4});
  const spio::ToolInstallResult install_v5 = spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);
  ASSERT_EQ(
      spio::RunCli({
          "tool",
          "pin",
          "--manifest-path",
          manifest_path.string(),
          "--version",
          "0.0.4",
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "status",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool status");
  EXPECT_EQ(payload.at("schema_version").get<int>(), 1);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "project-pin");
  EXPECT_EQ(payload.at("toolchain").at("version").get<std::string>(), "0.0.4");
  ASSERT_TRUE(payload.at("project_pin").is_object());
  EXPECT_EQ(payload.at("project_pin").at("path").get<std::string>(), (root / "project/spio-toolchain.toml").string());
  EXPECT_EQ(payload.at("project_pin").at("version").get<std::string>(), "0.0.4");
  EXPECT_EQ(payload.at("project_pin").at("install_present").get<bool>(), true);
  ASSERT_TRUE(payload.at("active_compiler").is_object());
  EXPECT_EQ(payload.at("active_compiler").at("compiler_version").get<std::string>(), "0.0.4");
  ASSERT_TRUE(payload.at("current_compiler").is_object());
  EXPECT_EQ(payload.at("current_compiler").at("compiler_version").get<std::string>(), "0.0.5");
  ASSERT_TRUE(payload.at("managed_toolchains").is_object());
  EXPECT_EQ(payload.at("managed_toolchains").at("current_binary").get<std::string>(), install_v5.managed_binary_path.string());
  ASSERT_EQ(payload.at("managed_toolchains").at("installed").size(), 2U);
  EXPECT_EQ(payload.at("notes").size(), 0U);
  EXPECT_EQ(
      payload.at("project_pin").at("install_binary_path").get<std::string>(),
      CanonicalAbsolutePath(install_v4.install_binary_path).string());
}

TEST(ToolStatusTests, SupportsCommandLocalJsonFlag)
{
  const fs::path root = MakeTempDir("tool-status-command-local-json");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v5, "0.0.5");
  (void) spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "tool",
      "status",
      "--json",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool status");
  EXPECT_EQ(payload.at("schema_version").get<int>(), 1);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "managed-current");
}

TEST(ToolStatusTests, ReportsMissingPinnedInstallWithoutFailing)
{
  const fs::path root = MakeTempDir("tool-status-missing-pin");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);
  WriteFile(
      root / "project/spio-toolchain.toml",
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.5\"\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "status",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "project-pin");
  EXPECT_TRUE(payload.at("active_compiler").is_null());
  EXPECT_TRUE(payload.at("current_compiler").is_null());
  ASSERT_TRUE(payload.at("project_pin").is_object());
  EXPECT_EQ(payload.at("project_pin").at("install_present").get<bool>(), false);
  ASSERT_FALSE(payload.at("notes").empty());
  EXPECT_NE(
      payload.at("notes")[0].get<std::string>().find("active compiler is unresolved"),
      std::string::npos);
}

TEST(ToolStatusTests, ExplicitStyioBinOverridesProjectPinPreview)
{
  const fs::path root = MakeTempDir("tool-status-explicit-styio");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path managed_styio = root / "styio-0.0.4";
  const fs::path explicit_styio = root / "styio-0.0.6";
  WriteFakeStyio(managed_styio, "0.0.4");
  WriteFakeStyio(explicit_styio, "0.0.6");

  (void) spio::InstallManagedStyio({.styio_binary = managed_styio});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);
  WriteFile(
      root / "project/spio-toolchain.toml",
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.4\"\n");

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "status",
      "--manifest-path",
      manifest_path.string(),
      "--styio-bin",
      explicit_styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  ASSERT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("toolchain").at("source").get<std::string>(), "environment");
  EXPECT_EQ(payload.at("toolchain").at("candidate_binary_path").get<std::string>(), CanonicalAbsolutePath(explicit_styio).string());
  ASSERT_TRUE(payload.at("active_compiler").is_object());
  EXPECT_EQ(payload.at("active_compiler").at("compiler_version").get<std::string>(), "0.0.6");
  ASSERT_TRUE(payload.at("project_pin").is_object());
  EXPECT_EQ(payload.at("project_pin").at("version").get<std::string>(), "0.0.4");
}

TEST(ToolStatusTests, HandlesLargeCompilerProbeStderrWithoutHanging)
{
  const fs::path root = MakeTempDir("tool-status-noisy-probe");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path noisy_styio = root / "styio-noisy";
  WriteFakeStyio(noisy_styio, "0.0.6", "stable", "2026", true, 2048);

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0)
  {
    testing::internal::CaptureStdout();
    const int exit_code = spio::RunCli({
        "--json",
        "tool",
        "status",
        "--styio-bin",
        noisy_styio.string(),
    });
    const std::string stdout_text = testing::internal::GetCapturedStdout();
    if (exit_code != spio::kExitSuccess)
    {
      _exit(10);
    }

    try
    {
      const json payload = json::parse(stdout_text);
      if (!payload.contains("active_compiler") || !payload.at("active_compiler").is_object())
      {
        _exit(11);
      }
      if (payload.at("active_compiler").at("compiler_version").get<std::string>() != "0.0.6")
      {
        _exit(12);
      }
    }
    catch (const std::exception &)
    {
      _exit(13);
    }
    _exit(0);
  }

  int status = 0;
  bool completed = false;
  for (int attempt = 0; attempt < 50; ++attempt)
  {
    const pid_t waited = waitpid(child, &status, WNOHANG);
    ASSERT_NE(waited, -1);
    if (waited == child)
    {
      completed = true;
      break;
    }
    usleep(100000);
  }

  if (!completed)
  {
    kill(child, SIGKILL);
    (void) waitpid(child, &status, 0);
    FAIL() << "tool status hung while probing compiler stdout/stderr";
  }

  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ToolInstallTests, ToolUseSwitchesManagedCurrentVersion)
{
  const fs::path root = MakeTempDir("tool-use-switch");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  (void) spio::InstallManagedStyio({.styio_binary = styio_v4});
  (void) spio::InstallManagedStyio({.styio_binary = styio_v5});

  testing::internal::CaptureStdout();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "use",
      "--version",
      "0.0.4",
      "--channel",
      "stable",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, spio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool use");
  EXPECT_EQ(payload.at("compiler_version").get<std::string>(), "0.0.4");

  const fs::path current_metadata = payload.at("current_metadata_path").get<std::string>();
  const json metadata = json::parse(ReadFile(current_metadata));
  EXPECT_EQ(metadata.at("compiler_version").get<std::string>(), "0.0.4");

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(payload.at("managed_binary_path").get<std::string>()));
}

TEST(ToolInstallTests, ToolUseRejectsMissingManagedVersion)
{
  const fs::path root = MakeTempDir("tool-use-missing-version");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  testing::internal::CaptureStderr();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "use",
      "--version",
      "0.0.9",
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, spio::kExitToolInstall);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ToolError");
}

TEST(ToolInstallTests, RejectsCompilerOutsidePublishedCompatibilityMatrix)
{
  const fs::path root = MakeTempDir("rejects-incompatible");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());
  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.1.2", "stable", "2026", false);

  testing::internal::CaptureStderr();
  const int exit_code = spio::RunCli({
      "--json",
      "tool",
      "install",
      "--styio-bin",
      fake_styio.string(),
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, spio::kExitContract);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ContractError");
}

TEST(ToolPinTests, ProjectPinOverridesManagedCurrentCompiler)
{
  const fs::path root = MakeTempDir("tool-pin-project-override");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  const spio::ToolInstallResult install_v4 = spio::InstallManagedStyio({.styio_binary = styio_v4});
  const spio::ToolInstallResult install_v5 = spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  testing::internal::CaptureStdout();
  const int pin_exit = spio::RunCli({
      "--json",
      "tool",
      "pin",
      "--manifest-path",
      manifest_path.string(),
      "--version",
      "0.0.4",
  });
  const std::string pin_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(pin_exit, spio::kExitSuccess);
  const json pin_payload = json::parse(pin_stdout);
  EXPECT_EQ(pin_payload.at("command").get<std::string>(), "tool pin");
  EXPECT_EQ(pin_payload.at("channel").get<std::string>(), "stable");
  EXPECT_EQ(pin_payload.at("compiler_version").get<std::string>(), "0.0.4");

  const fs::path pin_path = pin_payload.at("pin_path").get<std::string>();
  EXPECT_EQ(
      ReadFile(pin_path),
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.4\"\n");

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(install_v4.install_binary_path));
  EXPECT_NE(*resolved, CanonicalAbsolutePath(install_v5.managed_binary_path));

  testing::internal::CaptureStdout();
  const int check_exit = spio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string check_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(check_exit, spio::kExitSuccess);
  const json check_payload = json::parse(check_stdout);
  EXPECT_EQ(check_payload.at("styio").at("binary").get<std::string>(), install_v4.install_binary_path.string());
}

TEST(ToolPinTests, EnvironmentVariableStillOverridesProjectPin)
{
  const fs::path root = MakeTempDir("tool-pin-env-override");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  WriteFakeStyio(styio_v4, "0.0.4");
  (void) spio::InstallManagedStyio({.styio_binary = styio_v4});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "tool",
          "pin",
          "--manifest-path",
          manifest_path.string(),
          "--version",
          "0.0.4",
      }),
      spio::kExitSuccess);

  const fs::path explicit_styio = root / "explicit-styio";
  WriteFakeStyio(explicit_styio, "0.0.6");
  const ScopedEnvVar styio_bin("SPIO_STYIO_BIN", explicit_styio.string());

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(explicit_styio));
}

TEST(ToolPinTests, ClearRemovesProjectPinAndFallsBackToManagedCurrent)
{
  const fs::path root = MakeTempDir("tool-pin-clear");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path styio_v4 = root / "styio-0.0.4";
  const fs::path styio_v5 = root / "styio-0.0.5";
  WriteFakeStyio(styio_v4, "0.0.4");
  WriteFakeStyio(styio_v5, "0.0.5");

  const spio::ToolInstallResult install_v4 = spio::InstallManagedStyio({.styio_binary = styio_v4});
  const spio::ToolInstallResult install_v5 = spio::InstallManagedStyio({.styio_binary = styio_v5});

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);

  EXPECT_EQ(
      spio::RunCli({
          "tool",
          "pin",
          "--manifest-path",
          manifest_path.string(),
          "--version",
          "0.0.4",
      }),
      spio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int clear_exit = spio::RunCli({
      "--json",
      "tool",
      "pin",
      "--manifest-path",
      manifest_path.string(),
      "--clear",
  });
  const std::string clear_stdout = testing::internal::GetCapturedStdout();

  EXPECT_EQ(clear_exit, spio::kExitSuccess);
  const json clear_payload = json::parse(clear_stdout);
  EXPECT_EQ(clear_payload.at("mode").get<std::string>(), "clear");
  EXPECT_FALSE(fs::exists(clear_payload.at("pin_path").get<std::string>()));

  const std::optional<fs::path> resolved = spio::ResolveStyioBinary(std::nullopt, manifest_path);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, CanonicalAbsolutePath(install_v5.managed_binary_path));
  EXPECT_NE(*resolved, CanonicalAbsolutePath(install_v4.install_binary_path));
}

TEST(ToolPinTests, CheckFailsWhenPinnedManagedCompilerIsMissing)
{
  const fs::path root = MakeTempDir("tool-pin-missing-compiler");
  const ScopedEnvVar spio_home("SPIO_HOME", (root / ".spio-home").string());

  const fs::path manifest_path = root / "project/spio.toml";
  WriteSingleBinManifest(manifest_path);
  WriteFile(
      root / "project/spio-toolchain.toml",
      "[styio]\n"
      "channel = \"stable\"\n"
      "version = \"0.0.5\"\n");

  testing::internal::CaptureStderr();
  const int exit_code = spio::RunCli({
      "--json",
      "check",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stderr_text = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, spio::kExitToolInstall);
  const json payload = json::parse(stderr_text);
  EXPECT_EQ(payload.at("category").get<std::string>(), "ToolError");
}
