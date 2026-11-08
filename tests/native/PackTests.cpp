#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioPack/Pack.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "BuildTestSupport.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::ChildProcessResult;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::RunChildProcess;
using pafio::testsupport::WriteFile;

namespace
{

std::vector<std::string> ListTarEntries(const fs::path &archive_path)
{
  const ChildProcessResult result = RunChildProcess("tar", {"-tf", archive_path.string()});
  EXPECT_EQ(result.exit_code, 0) << result.stderr_text;

  std::vector<std::string> entries;
  std::istringstream in(result.stdout_text);
  for (std::string line; std::getline(in, line);)
  {
    if (!line.empty())
    {
      entries.push_back(line);
    }
  }
  return entries;
}

std::string ReadTarFile(const fs::path &archive_path, const std::string &member)
{
  const ChildProcessResult result = RunChildProcess("tar", {"-xOf", archive_path.string(), member});
  EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
  return result.stdout_text;
}

}  // namespace

TEST(PackTests, WritesDeterministicSourceArchiveForSinglePackage)
{
  const fs::path root = MakeTempDir("single-package");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / "README.md", "# demo\n");
  WriteFile(root / "pafio.lock", "lock-version = 1\n");
  WriteFile(root / ".pafio/cache/state.json", "{}\n");
  WriteFile(root / ".git/HEAD", "ref: refs/heads/main\n");
  WriteFile(root / "dist/old.tar", "old\n");
  WriteFile(root / "build-codex/tmp.txt", "generated\n");

  const pafio::PackResult result = pafio::WriteSourcePackage({
      .manifest_path = root / "pafio.toml",
  });

  EXPECT_TRUE(fs::exists(result.archive_path));
  EXPECT_EQ(result.package_name, "acme/app");
  EXPECT_EQ(result.archive_prefix, "app-0.1.0");
  EXPECT_EQ(result.file_count, 3U);

  const std::vector<std::string> entries = ListTarEntries(result.archive_path);
  EXPECT_EQ(
      entries,
      std::vector<std::string>({
          "app-0.1.0/README.md",
          "app-0.1.0/pafio.toml",
          "app-0.1.0/src/main.styio",
      }));

  EXPECT_EQ(
      ReadTarFile(result.archive_path, "app-0.1.0/pafio.toml"),
      std::string(
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
          "path = \"src/main.styio\"\n"));
}

TEST(PackTests, WorkspaceRootPackageArchiveExcludesWorkspaceMembers)
{
  const fs::path root = MakeTempDir("combined-workspace-root");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/root\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[workspace]\n"
      "members = [\"packages/util\"]\n"
      "exclude = [\"vendor\"]\n"
      "resolver = \"1\"\n");
  WriteFile(root / "src/lib.styio", "# lib := 1\n");
  WriteFile(
      root / "packages/util/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/util\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "packages/util/src/lib.styio", "# util := 1\n");
  WriteFile(root / "vendor/third_party.txt", "skip\n");

  const pafio::PackResult result = pafio::WriteSourcePackage({
      .manifest_path = root / "pafio.toml",
  });

  const std::vector<std::string> entries = ListTarEntries(result.archive_path);
  EXPECT_EQ(
      entries,
      std::vector<std::string>({
          "root-0.1.0/pafio.toml",
          "root-0.1.0/src/lib.styio",
      }));
}

TEST(PackTests, WorkspaceOnlyPackRequiresExplicitPackageSelectionWhenAmbiguous)
{
  const fs::path root = MakeTempDir("workspace-ambiguity");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[workspace]\n"
      "members = [\"packages/app\", \"packages/tool\"]\n"
      "resolver = \"1\"\n");
  WriteFile(
      root / "packages/app/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/app/src/main.styio", ">_(\"app\")\n");
  WriteFile(
      root / "packages/tool/pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/tool\"\n"
      "version = \"0.2.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"tool\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "packages/tool/src/main.styio", ">_(\"tool\")\n");

  EXPECT_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PackError);

  testing::internal::CaptureStdout();
  const int exit_code = pafio::RunCli({
      "--json",
      "pack",
      "--manifest-path",
      (root / "pafio.toml").string(),
      "--package",
      "acme/tool",
  });
  const std::string stdout_text = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, pafio::kExitSuccess);
  const json payload = json::parse(stdout_text);
  EXPECT_EQ(payload.at("command").get<std::string>(), "pack");
  EXPECT_EQ(payload.at("package").get<std::string>(), "acme/tool");
  EXPECT_EQ(payload.at("version").get<std::string>(), "0.2.0");
  EXPECT_TRUE(fs::exists(payload.at("archive_path").get<std::string>()));
}

TEST(PackTests, SkipsHiddenLocalStateInIncludedTree)
{
  const fs::path root = MakeTempDir("hidden-local-state");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / ".env", "TOKEN=local-only\n");
  WriteFile(root / ".hidden/nested.env", "TOKEN=local-only\n");

  const pafio::PackResult result = pafio::WriteSourcePackage({
      .manifest_path = root / "pafio.toml",
  });

  const std::vector<std::string> entries = ListTarEntries(result.archive_path);
  EXPECT_EQ(
      entries,
      std::vector<std::string>({
          "app-0.1.0/pafio.toml",
          "app-0.1.0/src/main.styio",
      }));
}

TEST(PackTests, RejectsCredentialNamedFileInIncludedTree)
{
  const fs::path root = MakeTempDir("credential-name-rejected");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / "certs/server.pem", "placeholder\n");

  EXPECT_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PackError);
}

TEST(PackTests, RejectsCredentialShapedFileContents)
{
  const fs::path root = MakeTempDir("credential-content-rejected");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / "notes.txt", std::string("-----BEGIN TEST ") + "PRIVATE KEY-----\n");

  EXPECT_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PackError);
}

TEST(PackTests, RejectsJwtShapedFileContents)
{
  const fs::path root = MakeTempDir("jwt-content-rejected");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  // The JWT is assembled at test runtime so the credential-shaped literal
  // never exists in the source tree while still exercising the detector.
  const std::string jwt =
      std::string("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9") + "." +
      "eyJzdWIiOiIxMjM0NTY3ODkwIn0" + "." + "c2lnbmF0dXJlcGF5bG9hZA";
  WriteFile(root / "notes.txt", "token: " + jwt + "\n");

  EXPECT_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PackError);
}

TEST(PackTests, IgnoresPlantedSymlinkAtPredictableTempPath)
{
  const fs::path root = MakeTempDir("temp-symlink-ignored");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / "victim.txt", "keep me\n");

  const fs::path archive = root / "dist" / "app.tar";
  fs::create_directories(archive.parent_path());
  const fs::path planted = archive.string() + ".tmp";
  fs::create_symlink(root / "victim.txt", planted);

  EXPECT_NO_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
          .output_path = archive,
      }));
  EXPECT_TRUE(fs::is_symlink(planted));
  EXPECT_EQ(ReadFile(root / "victim.txt"), "keep me\n");
  EXPECT_TRUE(fs::exists(archive));
}

TEST(PackTests, ExclusiveTempCreateLeavesPlantedDecoysUntouched)
{
  const fs::path root = MakeTempDir("temp-decoys-untouched");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  WriteFile(root / "victim.txt", "keep me\n");

  const fs::path archive = root / "dist" / "app.tar";
  fs::create_directories(archive.parent_path());
  const fs::path decoy_file = archive.parent_path() / "app.tar.tmp.1.1";
  const fs::path decoy_link = archive.parent_path() / "app.tar.tmp.2.2";
  WriteFile(decoy_file, "decoy\n");
#if !defined(_WIN32)
  fs::create_symlink(root / "victim.txt", decoy_link);
#else
  WriteFile(decoy_link, "decoy\n");
#endif

  EXPECT_NO_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
          .output_path = archive,
      }));
  EXPECT_EQ(ReadFile(decoy_file), "decoy\n");
#if !defined(_WIN32)
  EXPECT_TRUE(fs::is_symlink(decoy_link));
#endif
  EXPECT_EQ(ReadFile(root / "victim.txt"), "keep me\n");
  EXPECT_TRUE(fs::exists(archive));
}

TEST(PackTests, RejectsSymlinkInsideIncludedTree)
{
  const fs::path root = MakeTempDir("symlink-rejected");
  WriteFile(
      root / "pafio.toml",
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n");
  WriteFile(root / "src/main.styio", ">_(\"hello\")\n");
  fs::create_symlink(root / "src/main.styio", root / "src/main-link.styio");

  EXPECT_THROW(
      pafio::WriteSourcePackage({
          .manifest_path = root / "pafio.toml",
      }),
      pafio::PackError);
}
