#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioTool/Install.hpp"

#include "ToolTestSupport.hpp"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace pafio_test_support;

TEST(ToolStatusTests, StatusReportsManagedCompilerPinAndCloudPolicy)
{
  const fs::path root = MakeTempDir("tool-status-json");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path manifest_path = root / "project/pafio.toml";
  WriteSingleBinManifest(manifest_path);

  const fs::path fake_styio = root / "fake-styio";
  WriteFakeStyio(fake_styio, "0.0.5");
  (void) pafio::InstallManagedStyio({.styio_binary = fake_styio});
  (void) pafio::PinManagedStyio({
      .manifest_path = manifest_path,
      .compiler_version = std::string("0.0.5"),
      .compiler_channel = std::string("stable"),
  });

  EXPECT_EQ(
      pafio::RunCli({
          "set",
          "risk",
          "as",
          "trusted-internal",
          "--manifest-path",
          manifest_path.string(),
      }),
      pafio::kExitSuccess);
  EXPECT_EQ(
      pafio::RunCli({
          "set",
          "lane",
          "as",
          "warm-shared",
          "--manifest-path",
          manifest_path.string(),
      }),
      pafio::kExitSuccess);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "tool",
      "status",
      "--json",
      "--manifest-path",
      manifest_path.string(),
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "tool status");
  EXPECT_EQ(payload.at("project_pin").at("compiler_version").get<std::string>(), "0.0.5");
  EXPECT_EQ(payload.at("current_compiler").at("compiler_version").get<std::string>(), "0.0.5");
  ASSERT_FALSE(payload.at("managed_toolchains").empty());
  EXPECT_TRUE(payload.at("managed_toolchains")[0].at("current").get<bool>());
  EXPECT_EQ(payload.at("toolchain_state").at("risk_class").get<std::string>(), "trusted-internal");
  EXPECT_EQ(payload.at("cloud").at("execution_lane").get<std::string>(), "warm-shared");
}
