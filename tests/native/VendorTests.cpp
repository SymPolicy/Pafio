#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"

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

std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
}

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

fs::path MakeTempDir(const std::string &label)
{
  const fs::path root = fs::temp_directory_path() / "pafio-native-vendor-tests" / label;
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

void RunGitOrAssert(const std::vector<std::string> &args)
{
  const ChildProcessResult result = RunChildProcess("git", args);
  ASSERT_EQ(result.exit_code, 0) << TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
}

std::string GitHeadRev(const fs::path &repo_root)
{
  const ChildProcessResult result = RunChildProcess("git", {"-C", repo_root.string(), "rev-parse", "HEAD"});
  EXPECT_EQ(result.exit_code, 0) << TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
  return TrimTrailingNewline(result.stdout_text);
}

std::string CreateWorkspaceGitRepo(const fs::path &repo_root, const std::string &util_version)
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
      "[toolchain]\n"
      "channel = \"nightly\"\n"
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
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[lib]\n"
          "path = \"src/lib.styio\"\n");

  RunGitOrAssert(
      {"-C", repo_root.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "add", "."});
  RunGitOrAssert(
      {"-C", repo_root.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "commit", "--quiet", "-m", "initial"});
  return GitHeadRev(repo_root);
}

}  // namespace

TEST(VendorCliTests, WritesProjectLocalVendorSnapshotsAndMetadata)
{
  const fs::path root = MakeTempDir("vendor-write");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      std::string(
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
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "feed = { package = \"acme/feed\", git = \"") +
          CanonicalAbsolutePath(git_repo).generic_string() +
          "\", rev = \"" + rev + "\" }\n");

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "vendor", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  const fs::path vendor_root = root / ".pafio" / "vendor";
  EXPECT_EQ(payload.at("command").get<std::string>(), "vendor");
  EXPECT_EQ(payload.at("vendor_root").get<std::string>(), CanonicalAbsolutePath(vendor_root).string());
  EXPECT_EQ(payload.at("git_snapshots").get<size_t>(), 1U);
  EXPECT_TRUE(fs::exists(vendor_root / "pafio-vendor.json"));

  const json metadata = json::parse(ReadFile(vendor_root / "pafio-vendor.json"));
  ASSERT_EQ(metadata.at("git_snapshots").size(), 1U);
  EXPECT_EQ(metadata.at("git_snapshots")[0].at("rev").get<std::string>(), rev);
  EXPECT_TRUE(fs::exists(metadata.at("git_snapshots")[0].at("path").get<std::string>()));
}

TEST(FetchCliTests, OfflineUsesVendoredSnapshotsWithoutGlobalCache)
{
  const fs::path root = MakeTempDir("vendor-offline-fetch");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      std::string(
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
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "feed = { package = \"acme/feed\", git = \"") +
          CanonicalAbsolutePath(git_repo).generic_string() +
          "\", rev = \"" + rev + "\" }\n");

  ASSERT_EQ(pafio::RunCli({"vendor", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  fs::remove_all(root / ".pafio-home");

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "fetch", "--offline", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_TRUE(payload.at("offline").get<bool>());
  EXPECT_EQ(payload.at("packages").get<size_t>(), 3U);
  EXPECT_EQ(payload.at("git_packages").get<size_t>(), 2U);
}

TEST(CheckCliTests, LockedRequiresAdjacentLockfile)
{
  const fs::path root = MakeTempDir("locked-check-missing-lock");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
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

  EXPECT_EQ(pafio::RunCli({"check", "--locked", "--manifest-path", manifest_path.string()}), pafio::kExitLock);
}

TEST(BuildCliTests, FrozenDryRunRequiresAdjacentLockfile)
{
  const fs::path root = MakeTempDir("frozen-build-missing-lock");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
      manifest_path,
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/demo\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n"
      "publish = false\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n");
  WriteFile(root / "src/lib.styio", "# value := 1\n");

  EXPECT_EQ(pafio::RunCli({"build", "--dry-run", "--frozen", "--manifest-path", manifest_path.string(), "--lib"}), pafio::kExitLock);
}
