#include "BuildTestSupport.hpp"

#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteExecutable;
using pafio::testsupport::WriteFile;

namespace
{

std::map<std::string, std::string> SnapshotTree(const fs::path &root)
{
  std::map<std::string, std::string> snapshot;
  if (!fs::exists(root))
  {
    return snapshot;
  }
  for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root))
  {
    const std::string relative = fs::relative(entry.path(), root).generic_string();
    snapshot.emplace(relative, entry.is_regular_file() ? ReadFile(entry.path()) : "<directory>");
  }
  return snapshot;
}

}  // namespace

TEST(DoctorTests, DiagnosesOwnedStateWithoutRepairingOrInstallingAnything)
{
  const fs::path root = MakeTempDir("doctor-read-only");
  const fs::path home = root / ".pafio-home";
  const ScopedEnvVar pafio_home("PAFIO_HOME", home.string());
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  const fs::path styio = root / "styio";
  WriteExecutable(
      styio,
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
      "  printf '%s\\n' '{\"tool\":\"styio\",\"compiler_version\":\"0.0.5\",\"channel\":\"stable\",\"active_integration_phase\":\"compile-plan-live\",\"supported_contract_versions\":{\"machine_info\":[1],\"jsonl_diagnostics\":[1],\"compile_plan\":[1],\"runtime_events\":[]},\"supported_adapter_modes\":[\"cli\"],\"feature_flags\":{\"single_file_entry\":true,\"jsonl_diagnostics\":true,\"compile_plan_consumer\":true,\"project_execution_via_compile_plan\":true,\"runtime_event_stream\":false},\"supported_contracts\":{\"compile_plan\":[1]},\"capabilities\":[\"machine_info_json\",\"single_file_entry\",\"jsonl_diagnostics\"],\"edition_max\":\"2026\"}'\n"
      "  exit 0\n"
      "fi\n"
      "exit 64\n");

  const auto before = SnapshotTree(root);
  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "doctor",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--styio-bin",
      styio.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();
  const auto after = SnapshotTree(root);

  EXPECT_NE(exit_code, pafio::kExitUsage);
  ASSERT_FALSE(stdout_text.empty());
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command"), "doctor");
  ASSERT_TRUE(payload.at("checks").is_array());

  const std::set<std::string> allowed_checks{
      "manifest",
      "lock",
      "resolution",
      "cache",
      "registry_trust",
      "styio",
  };
  std::set<std::string> observed_checks;
  for (const json &check : payload.at("checks"))
  {
    const std::string name = check.at("name").get<std::string>();
    EXPECT_TRUE(allowed_checks.contains(name)) << name;
    observed_checks.insert(name);
  }
  EXPECT_EQ(observed_checks, allowed_checks);
  EXPECT_FALSE(payload.contains("release_root"));
  EXPECT_FALSE(payload.contains("tool_status"));
  EXPECT_FALSE(payload.contains("managed_toolchain"));
  EXPECT_EQ(after, before);
  EXPECT_FALSE(fs::exists(root / "pafio.lock"));
  EXPECT_FALSE(fs::exists(root / ".pafio"));
  EXPECT_FALSE(fs::exists(home));
}
