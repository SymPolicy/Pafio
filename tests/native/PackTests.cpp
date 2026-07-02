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

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

struct ChildProcessResult
{
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

ChildProcessResult RunChildProcess(const std::string &binary, const std::vector<std::string> &args)
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
  if (WIFEXITED(status))
  {
    result.exit_code = WEXITSTATUS(status);
  }
  else
  {
    result.exit_code = 1;
  }

  return result;
}

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

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "pafio-native-pack-tests" / label;
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
          "[toolchain]\n"
          "channel = \"nightly\"\n"
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
