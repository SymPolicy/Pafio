#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "PafioCLI/CLI.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioPack/Pack.hpp"
#include "PafioResolve/Resolver.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"

#include "BuildTestSupport.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::CanonicalAbsolutePath;
using pafio::testsupport::ChildProcessResult;
using pafio::testsupport::CreateWorkspaceGitRepo;
using pafio::testsupport::GitHeadRev;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::RunChildProcess;
using pafio::testsupport::RunGitOrAssert;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::TrimTrailingNewline;
using pafio::testsupport::WriteFile;

namespace
{

std::string
FileUrl(const fs::path &path) {
  return "file://" + CanonicalAbsolutePath(path).generic_string();
}

struct CachedRegistryPackage
{
  std::string id;
  std::string name;
  std::string version;
  std::string registry;
  std::string sha256;
};

CachedRegistryPackage
SeedVerifiedRegistryCache(
  const fs::path &manifest_path,
  const fs::path &pafio_home,
  const std::string &registry_url
) {
  const pafio::ManifestDocument manifest = pafio::LoadManifest(manifest_path);
  if (!manifest.package.has_value()) {
    throw std::runtime_error("registry cache fixture requires a package manifest");
  }
  const pafio::PackageConfig &package = *manifest.package;
  const pafio::RegistryPackageNameParts name =
    pafio::SplitRegistryPackageName(package.name, "registry cache fixture");
  const fs::path artifact_path =
    pafio_home / "fixture-artifacts" /
    (name.namespace_name + "-" + name.short_name + "-" + package.version + ".tar");
  const pafio::PackResult packed = pafio::WriteSourcePackage({
    .manifest_path = manifest_path,
    .output_path = artifact_path,
  });
  const std::string digest = pafio::Sha256File(packed.archive_path);
  const fs::path blob_path =
    pafio::RegistryBlobCacheRoot(pafio_home) /
    digest.substr(0U, 2U) /
    digest.substr(2U, 2U) /
    (digest + ".tar");
  fs::create_directories(blob_path.parent_path());
  fs::copy_file(
    packed.archive_path,
    blob_path,
    fs::copy_options::overwrite_existing
  );

  const fs::path checkout_root =
    pafio::RegistryCheckoutRoot(pafio_home) /
    name.namespace_name /
    name.short_name /
    package.version /
    digest;
  fs::create_directories(checkout_root);
  const ChildProcessResult extracted = RunChildProcess(
    pafio::ResolvedTarPath(),
    {"-xf", packed.archive_path.string(), "-C", checkout_root.string()}
  );
  if (extracted.exit_code != 0) {
    throw std::runtime_error(
      "failed to extract registry cache fixture: " +
      TrimTrailingNewline(
        extracted.stderr_text.empty()
          ? extracted.stdout_text
          : extracted.stderr_text
      )
    );
  }
  WriteFile(
    checkout_root / ".pafio-snapshot-ready",
    json({
      {"schema_version", 1},
      {"sha256", digest},
      {"size_bytes", fs::file_size(blob_path)},
    }).dump(2) + "\n"
  );

  return {
    .id = "registry:" + package.name + "@" + package.version + "#" + digest,
    .name = package.name,
    .version = package.version,
    .registry = registry_url,
    .sha256 = digest,
  };
}

void
WriteRegistryLockHints(
  const fs::path &lockfile_path,
  const std::vector<std::pair<CachedRegistryPackage, std::vector<std::string>>> &packages
) {
  pafio::LockfileDocument lockfile{
    .generated_by = "pafio registry cache fixture",
    .resolver = "single-version-v1",
  };
  for (const auto &[package, dependencies] : packages) {
    lockfile.packages.push_back({
      .id = package.id,
      .name = package.name,
      .version = package.version,
      .source_kind = "registry",
      .registry = package.registry,
      .sha256 = package.sha256,
      .dependencies = dependencies,
    });
  }
  WriteFile(lockfile_path, pafio::SerializeLockfileCanonical(lockfile));
}

std::optional<std::string>
TryResolveLockfileMessage(const fs::path &manifest_path) {
  try {
    (void)pafio::ResolveSingleVersionLockfile(manifest_path);
    return std::nullopt;
  }
  catch (const pafio::ResolutionError &error) {
    return std::string(error.what());
  }
}


}  // namespace

TEST(LockGenerationTests, GeneratesLockfileForSinglePackage) {
  const fs::path root = MakeTempDir("single-package");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
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
    "path = \"src/main.styio\"\n"
  );

  const auto generated = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");

  EXPECT_EQ(generated.lockfile.generated_by, "pafio 0.1.0-dev");
  EXPECT_EQ(generated.lockfile.resolver, "single-version-v1");
  ASSERT_EQ(generated.lockfile.packages.size(), 1U);
  EXPECT_EQ(generated.lockfile.packages[0].id, "workspace:acme/app@0.1.0");
  EXPECT_EQ(generated.lockfile.packages[0].source_kind, "workspace");
  EXPECT_TRUE(generated.lockfile.packages[0].dependencies.empty());
}

TEST(LockGenerationTests, GeneratesWorkspaceAndPathGraph) {
  const fs::path root = MakeTempDir("workspace-and-path");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
    root / "pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[workspace]\n"
    "members = [\"packages/app\", \"packages/util\"]\n"
    "resolver = \"1\"\n"
  );
  WriteFile(
    root / "packages/app/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/app\"\n"
    "version = \"0.5.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "core = { package = \"acme/core\", path = \"../../vendor/core\" }\n\n"
    "[dev-dependencies]\n"
    "util = { package = \"acme/util\", path = \"../util\" }\n"
  );
  WriteFile(
    root / "packages/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );
  WriteFile(
    root / "vendor/core/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/core\"\n"
    "version = \"0.3.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  const auto generated = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");

  ASSERT_EQ(generated.lockfile.packages.size(), 3U);
  EXPECT_EQ(
    pafio::SerializeLockfileCanonical(generated.lockfile),
    std::string(
      "lock-version = 1\n\n"
      "[metadata]\n"
      "generated-by = \"pafio 0.1.0-dev\"\n"
      "resolver = \"single-version-v1\"\n\n"
      "[[package]]\n"
      "id = \"path:acme/core@0.3.0\"\n"
      "name = \"acme/core\"\n"
      "version = \"0.3.0\"\n"
      "source-kind = \"path\"\n"
      "dependencies = []\n\n"
      "[[package]]\n"
      "id = \"workspace:acme/app@0.5.0\"\n"
      "name = \"acme/app\"\n"
      "version = \"0.5.0\"\n"
      "source-kind = \"workspace\"\n"
      "dependencies = [\"path:acme/core@0.3.0\", \"workspace:acme/util@0.2.0\"]\n\n"
      "[[package]]\n"
      "id = \"workspace:acme/util@0.2.0\"\n"
      "name = \"acme/util\"\n"
      "version = \"0.2.0\"\n"
      "source-kind = \"workspace\"\n"
      "dependencies = []\n"
    )
  );
}

TEST(ResolverTests, ResolvesPinnedGitWorkspaceAndTransitivePathDependencies) {
  const fs::path root = MakeTempDir("git-workspace");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");

  WriteFile(
    root / "pafio.toml",
    std::string(
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", git = \""
    ) + CanonicalAbsolutePath(git_repo).generic_string()
      + "\", rev = \"" + rev + "\" }\n"
  );

  const auto generated = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");

  ASSERT_EQ(generated.lockfile.packages.size(), 3U);
  const std::string expected =
    std::string(
      "lock-version = 1\n\n"
      "[metadata]\n"
      "generated-by = \"pafio 0.1.0-dev\"\n"
      "resolver = \"single-version-v1\"\n\n"
      "[[package]]\n"
      "id = \"git:acme/feed@1.2.0#"
    )
    + rev +
    "\"\n"
    "name = \"acme/feed\"\n"
    "version = \"1.2.0\"\n"
    "source-kind = \"git\"\n"
    "git = \""
    + CanonicalAbsolutePath(git_repo).generic_string() +
    "\"\n"
    "rev = \""
    + rev +
    "\"\n"
    "dependencies = [\"git:acme/util@0.9.0#"
    + rev +
    "\"]\n\n"
    "[[package]]\n"
    "id = \"git:acme/util@0.9.0#"
    + rev +
    "\"\n"
    "name = \"acme/util\"\n"
    "version = \"0.9.0\"\n"
    "source-kind = \"git\"\n"
    "git = \""
    + CanonicalAbsolutePath(git_repo).generic_string() +
    "\"\n"
    "rev = \""
    + rev +
    "\"\n"
    "dependencies = []\n\n"
    "[[package]]\n"
    "id = \"workspace:acme/app@0.1.0\"\n"
    "name = \"acme/app\"\n"
    "version = \"0.1.0\"\n"
    "source-kind = \"workspace\"\n"
    "dependencies = [\"git:acme/feed@1.2.0#"
    + rev + "\"]\n";
  EXPECT_EQ(pafio::SerializeLockfileCanonical(generated.lockfile), expected);
}

TEST(ResolverTests, ResolvesLargeGitSnapshotsWithoutArchiveTruncation) {
  const fs::path root = MakeTempDir("git-workspace-large-snapshot");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  (void)CreateWorkspaceGitRepo(git_repo, "0.9.0");

  WriteFile(git_repo / "packages/feed/assets/large.txt", std::string(1U << 21, 'x'));
  RunGitOrAssert(
    {"-C", git_repo.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "add", "."}
  );
  RunGitOrAssert(
    {"-C", git_repo.string(), "-c", "user.email=pafio-tests@example.com", "-c", "user.name=pafio-tests", "commit", "--quiet", "-m", "large snapshot"}
  );
  const std::string rev = GitHeadRev(git_repo);

  WriteFile(
    root / "pafio.toml",
    std::string(
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", git = \""
    ) + CanonicalAbsolutePath(git_repo).generic_string()
      + "\", rev = \"" + rev + "\" }\n"
  );

  const auto generated = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");
  ASSERT_EQ(generated.lockfile.packages.size(), 3U);
  EXPECT_EQ(generated.lockfile.packages.back().source_kind, "workspace");
}

TEST(ResolverTests, ReportsFullCyclePathOnLockResolution) {
  const fs::path root = MakeTempDir("lock-cycle-path");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n\n"
    "[dependencies]\n"
    "app = { package = \"acme/app\", path = \"../..\" }\n"
  );

  const std::optional<std::string> message = TryResolveLockfileMessage(manifest_path);
  ASSERT_TRUE(message.has_value());
  EXPECT_NE(message->find("acyclic dependency graph"), std::string::npos);
  EXPECT_NE(message->find("workspace:acme/app@0.1.0"), std::string::npos);
  EXPECT_NE(message->find("path:acme/util@0.2.0"), std::string::npos);
}

TEST(ResolverTests, ReportsBothChainsOnDiamondVersionConflict) {
  const fs::path root = MakeTempDir("diamond-version-conflict");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());

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
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "left = { package = \"acme/left\", path = \"vendor/left\" }\n"
    "right = { package = \"acme/right\", path = \"vendor/right\" }\n"
  );
  WriteFile(
    root / "vendor/left/pafio.toml",
    std::string(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/left\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"../util-a\" }\n"
    )
  );
  WriteFile(
    root / "vendor/right/pafio.toml",
    std::string(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/right\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"../util-b\" }\n"
    )
  );
  WriteFile(
    root / "vendor/util-a/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.1.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );
  WriteFile(
    root / "vendor/util-b/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  const std::optional<std::string> message = TryResolveLockfileMessage(root / "pafio.toml");
  ASSERT_TRUE(message.has_value());
  EXPECT_NE(message->find("single-version-v1 conflict"), std::string::npos);
  EXPECT_NE(message->find("required by:"), std::string::npos);
  EXPECT_NE(message->find("0.1.0"), std::string::npos);
  EXPECT_NE(message->find("0.2.0"), std::string::npos);
  const size_t first_chain = message->find("required by:");
  ASSERT_NE(first_chain, std::string::npos);
  const size_t second_chain = message->find("required by:", first_chain + 1U);
  EXPECT_NE(second_chain, std::string::npos);
}

TEST(ResolverTests, ProducesDeterministicLockfileAcrossRepeatedResolution) {
  const fs::path root = MakeTempDir("golden-lock-repeat");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  WriteFile(
    root / "pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[workspace]\n"
    "members = [\"packages/app\", \"packages/util\"]\n"
    "resolver = \"1\"\n"
  );
  WriteFile(
    root / "packages/app/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/app\"\n"
    "version = \"0.5.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "core = { package = \"acme/core\", path = \"../../vendor/core\" }\n\n"
    "[dev-dependencies]\n"
    "util = { package = \"acme/util\", path = \"../util\" }\n"
  );
  WriteFile(
    root / "packages/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );
  WriteFile(
    root / "vendor/core/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/core\"\n"
    "version = \"0.3.1\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  const auto first = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");
  const auto second = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");

  ASSERT_EQ(first.lockfile.packages.size(), second.lockfile.packages.size());
  for (size_t i = 0; i < first.lockfile.packages.size(); ++i) {
    EXPECT_EQ(first.lockfile.packages[i].id, second.lockfile.packages[i].id);
    EXPECT_EQ(first.lockfile.packages[i].version, second.lockfile.packages[i].version);
    EXPECT_EQ(first.lockfile.packages[i].source_kind, second.lockfile.packages[i].source_kind);
    EXPECT_EQ(first.lockfile.packages[i].dependencies, second.lockfile.packages[i].dependencies);
  }
}

TEST(ResolverTests, RejectsSingleVersionConflictsAcrossPathAndGit) {
  const fs::path root = MakeTempDir("single-version-conflict");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");

  WriteFile(
    root / "pafio.toml",
    std::string(
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", git = \""
    ) + CanonicalAbsolutePath(git_repo).generic_string()
      + "\", rev = \"" + rev +
      "\" }\n"
      "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.1.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  EXPECT_THROW(pafio::ResolveSingleVersionLockfile(root / "pafio.toml"), pafio::ResolutionError);
}

TEST(ResolverTests, ResolvesRegistryGraphFromVerifiedCache) {
  const fs::path root = MakeTempDir("registry-workspace");
  const fs::path pafio_home_path = root / ".pafio-home";
  const ScopedEnvVar pafio_home("PAFIO_HOME", pafio_home_path.string());
  const fs::path registry_root = root / "registry";
  const std::string registry_url = FileUrl(registry_root);

  WriteFile(
    root / "fixtures/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n"
    "publish = true\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );
  WriteFile(root / "fixtures/util/src/lib.styio", "# util\n");
  const CachedRegistryPackage cached_util = SeedVerifiedRegistryCache(
    root / "fixtures/util/pafio.toml",
    pafio_home_path,
    registry_url
  );

  WriteFile(
    root / "fixtures/feed/pafio.toml",
    std::string(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/feed\"\n"
      "version = \"1.2.0\"\n"
      "edition = \"2026\"\n"
      "publish = true\n\n"
      "[build]\n"
      "implicit-std = true\n\n"
      "[lib]\n"
      "path = \"src/lib.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", version = \"0.2.0\", registry = \""
    ) + registry_url
      + "\" }\n"
  );
  WriteFile(root / "fixtures/feed/src/lib.styio", "# feed\n");
  const CachedRegistryPackage cached_feed = SeedVerifiedRegistryCache(
    root / "fixtures/feed/pafio.toml",
    pafio_home_path,
    registry_url
  );
  WriteRegistryLockHints(
    root / "pafio.lock",
    {
      {cached_util, {}},
      {cached_feed, {cached_util.id}},
    }
  );

  WriteFile(
    root / "pafio.toml",
    std::string(
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", version = \"1.2.0\", registry = \""
    ) + registry_url
      + "\" }\n"
  );

  const auto generated = pafio::ResolveSingleVersionLockfile(root / "pafio.toml");
  ASSERT_EQ(generated.lockfile.packages.size(), 3U);

  const auto find_package = [&](const std::string &name) -> const pafio::LockPackage &
  {
    const auto it = std::find_if(
      generated.lockfile.packages.begin(),
      generated.lockfile.packages.end(),
      [&](const pafio::LockPackage &package)
      {
        return package.name == name;
      }
    );
    EXPECT_NE(it, generated.lockfile.packages.end());
    return *it;
  };

  const pafio::LockPackage &feed = find_package("acme/feed");
  EXPECT_EQ(feed.source_kind, "registry");
  EXPECT_EQ(feed.registry.value_or(""), registry_url);
  ASSERT_TRUE(feed.sha256.has_value());
  EXPECT_EQ(feed.sha256->size(), 64U);
  EXPECT_TRUE(feed.id.starts_with("registry:acme/feed@1.2.0#"));
  ASSERT_EQ(feed.dependencies.size(), 1U);
  EXPECT_TRUE(feed.dependencies[0].starts_with("registry:acme/util@0.2.0#"));

  const pafio::LockPackage &util = find_package("acme/util");
  EXPECT_EQ(util.source_kind, "registry");
  EXPECT_EQ(util.registry.value_or(""), registry_url);
  ASSERT_TRUE(util.sha256.has_value());
  EXPECT_EQ(util.sha256->size(), 64U);

  const pafio::LockPackage &app = find_package("acme/app");
  EXPECT_EQ(app.source_kind, "workspace");
  ASSERT_EQ(app.dependencies.size(), 1U);
  EXPECT_EQ(app.dependencies[0], feed.id);

  EXPECT_TRUE(fs::exists(root / ".pafio-home" / "registry" / "blobs" / "sha256"));
  EXPECT_TRUE(fs::exists(root / ".pafio-home" / "registry" / "checkouts" / "acme" / "feed" / "1.2.0" / feed.sha256.value()));
}

TEST(TreeCliTests, RendersAsciiTreeForPinnedGitWorkspaceGraph) {
  const fs::path root = MakeTempDir("tree-git-workspace");
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
      "[build]\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "feed = { package = \"acme/feed\", git = \""
    ) + CanonicalAbsolutePath(git_repo).generic_string()
      + "\", rev = \"" + rev + "\" }\n"
  );

  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"tree", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const std::string output = testing::internal::GetCapturedStdout();

  EXPECT_EQ(
    output,
    std::string("workspace:acme/app@0.1.0\n") + "\\- git:acme/feed@1.2.0#" + rev + "\n" + "   \\- git:acme/util@0.9.0#" + rev + "\n"
  );
}

TEST(TreeCliTests, EmitsJsonGraphWithRootIds) {
  const fs::path root = MakeTempDir("tree-json");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n"
  );
  testing::internal::CaptureStdout();
  EXPECT_EQ(pafio::RunCli({"--json", "tree", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_EQ(payload.at("command").get<std::string>(), "tree");
  EXPECT_EQ(payload.at("manifest_path").get<std::string>(), CanonicalAbsolutePath(manifest_path).string());
  ASSERT_EQ(payload.at("root_ids").size(), 1U);
  EXPECT_EQ(payload.at("root_ids")[0].get<std::string>(), "workspace:acme/app@0.1.0");
  ASSERT_EQ(payload.at("packages").size(), 1U);
  EXPECT_EQ(payload.at("packages")[0].at("id").get<std::string>(), "workspace:acme/app@0.1.0");
  EXPECT_EQ(payload.at("packages")[0].at("dependencies").size(), 0U);
}

TEST(TreeCliTests, MarksCyclesInAsciiOutput) {
  // single-version-v1 is fail-closed on cycles: `pafio tree` must reject the
  // graph with a ResolutionError that includes the full cycle path, not render
  // a soft "(cycle)" marker under a successful exit.
  const fs::path root = MakeTempDir("tree-cycle");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n\n"
    "[dependencies]\n"
    "app = { package = \"acme/app\", path = \"../..\" }\n"
  );

  testing::internal::CaptureStderr();
  EXPECT_EQ(pafio::RunCli({"tree", "--manifest-path", manifest_path.string()}), 13);
  const std::string err = testing::internal::GetCapturedStderr();
  EXPECT_NE(err.find("acyclic dependency graph"), std::string::npos);
  EXPECT_NE(err.find("workspace:acme/app@0.1.0"), std::string::npos);
  EXPECT_NE(err.find("path:acme/util@0.2.0"), std::string::npos);
}

TEST(AddCliTests, AddsPathDependencyAndRefreshesLockfile) {
  const fs::path root = MakeTempDir("add-path");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  EXPECT_EQ(
    pafio::RunCli({"add", "acme/util", "--path", "vendor/util", "--manifest-path", manifest_path.string()}),
    pafio::kExitSuccess
  );

  EXPECT_EQ(
    ReadFile(manifest_path),
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
    )
  );

  const auto lockfile = pafio::LoadLockfile(root / "pafio.lock");
  ASSERT_EQ(lockfile.packages.size(), 2U);
  EXPECT_EQ(lockfile.packages[0].id, "path:acme/util@0.2.0");
  EXPECT_EQ(lockfile.packages[1].id, "workspace:acme/app@0.1.0");
}

TEST(AddCliTests, AddsGitDependencyToDevSectionAndRefreshesLockfile) {
  const fs::path root = MakeTempDir("add-git-dev");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
    manifest_path,
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
    "path = \"src/main.styio\"\n"
  );

  EXPECT_EQ(
    pafio::RunCli(
      {"add", "acme/feed", "--git", CanonicalAbsolutePath(git_repo).generic_string(), "--rev", rev, "--dev", "--manifest-path", manifest_path.string()}
    ),
    pafio::kExitSuccess
  );

  EXPECT_EQ(
    ReadFile(manifest_path),
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
      "path = \"src/main.styio\"\n\n"
      "[dev-dependencies]\n"
      "feed = { package = \"acme/feed\", git = \""
    ) + CanonicalAbsolutePath(git_repo).generic_string()
      + "\", rev = \"" + rev + "\" }\n"
  );

  const auto lockfile = pafio::LoadLockfile(root / "pafio.lock");
  ASSERT_EQ(lockfile.packages.size(), 3U);
}

TEST(AddCliTests, AddsCachedRegistryDependencyAndRefreshesLockfile) {
  const fs::path root = MakeTempDir("add-registry");
  const fs::path pafio_home_path = root / ".pafio-home";
  const ScopedEnvVar pafio_home("PAFIO_HOME", pafio_home_path.string());
  const fs::path registry_root = root / "registry";
  const std::string registry_url = FileUrl(registry_root);
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
    root / "fixtures/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n"
    "publish = true\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );
  WriteFile(root / "fixtures/util/src/lib.styio", "# util\n");
  const CachedRegistryPackage cached_util = SeedVerifiedRegistryCache(
    root / "fixtures/util/pafio.toml",
    pafio_home_path,
    registry_url
  );
  WriteRegistryLockHints(root / "pafio.lock", {{cached_util, {}}});

  WriteFile(
    manifest_path,
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
    "path = \"src/main.styio\"\n"
  );

  EXPECT_EQ(
    pafio::RunCli(
      {"add", "acme/util", "--registry", registry_url, "--version", "0.2.0", "--manifest-path", manifest_path.string()}
    ),
    pafio::kExitSuccess
  );

  EXPECT_EQ(
    ReadFile(manifest_path),
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
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "util = { package = \"acme/util\", version = \"0.2.0\", registry = \""
    ) + registry_url
      + "\" }\n"
  );

  const auto lockfile = pafio::LoadLockfile(root / "pafio.lock");
  ASSERT_EQ(lockfile.packages.size(), 2U);
  EXPECT_TRUE(lockfile.packages[0].id.starts_with("registry:acme/util@0.2.0#"));
  EXPECT_EQ(lockfile.packages[0].registry.value_or(""), registry_url);
  EXPECT_EQ(lockfile.packages[1].id, "workspace:acme/app@0.1.0");
}

TEST(AddCliTests, RollsBackManifestAndLockWhenResolutionFails) {
  const fs::path root = MakeTempDir("add-rollback");
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path git_repo = root / "remote-feed";
  const std::string rev = CreateWorkspaceGitRepo(git_repo, "0.9.0");
  const fs::path manifest_path = root / "pafio.toml";

  WriteFile(
    manifest_path,
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
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.1.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  ASSERT_EQ(pafio::RunCli({"sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  const std::string original_manifest = ReadFile(manifest_path);
  const std::string original_lock = ReadFile(root / "pafio.lock");

  EXPECT_EQ(
    pafio::RunCli({"add", "acme/feed", "--git", CanonicalAbsolutePath(git_repo).generic_string(), "--rev", rev, "--manifest-path", manifest_path.string()}),
    pafio::kExitResolve
  );
  EXPECT_EQ(ReadFile(manifest_path), original_manifest);
  EXPECT_EQ(ReadFile(root / "pafio.lock"), original_lock);
}

TEST(RemoveCliTests, RemovesDependencyByPackageNameAndRefreshesLockfile) {
  const fs::path root = MakeTempDir("remove-package");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  ASSERT_EQ(pafio::RunCli({"sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  EXPECT_EQ(
    pafio::RunCli({"remove", "acme/util", "--manifest-path", manifest_path.string()}),
    pafio::kExitSuccess
  );

  EXPECT_EQ(
    ReadFile(manifest_path),
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
      "path = \"src/main.styio\"\n"
    )
  );

  const auto lockfile = pafio::LoadLockfile(root / "pafio.lock");
  ASSERT_EQ(lockfile.packages.size(), 1U);
  EXPECT_EQ(lockfile.packages[0].id, "workspace:acme/app@0.1.0");
}

TEST(CheckCliTests, RejectsBrokenPathDependencyWithoutLockfile) {
  const fs::path root = MakeTempDir("check-broken-path");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/missing\" }\n"
  );

  EXPECT_EQ(pafio::RunCli({"check", "--manifest-path", manifest_path.string()}), pafio::kExitResolve);
}

TEST(CheckCliTests, RejectsStaleLockfileAgainstResolver) {
  const fs::path root = MakeTempDir("check-stale-lock");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n"
  );

  ASSERT_EQ(pafio::RunCli({"sync", "--manifest-path", manifest_path.string()}), pafio::kExitSuccess);
  WriteFile(
    manifest_path,
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
    "path = \"src/main.styio\"\n\n"
    "[dependencies]\n"
    "util = { package = \"acme/util\", path = \"vendor/util\" }\n"
  );
  WriteFile(
    root / "vendor/util/pafio.toml",
    "[pafio]\n"
    "manifest-version = 1\n\n"
    "[package]\n"
    "name = \"acme/util\"\n"
    "version = \"0.2.0\"\n"
    "edition = \"2026\"\n\n"
    "[build]\n"
    "implicit-std = true\n\n"
    "[lib]\n"
    "path = \"src/lib.styio\"\n"
  );

  EXPECT_EQ(
    pafio::RunCli({"check", "--manifest-path", manifest_path.string(), "--locked"}),
    pafio::kExitLock
  );
}

TEST(CheckCliTests, EmitsPackageCountForResolvedGraph) {
  const fs::path root = MakeTempDir("check-json");
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
    "[build]\n"
    "implicit-std = true\n\n"
    "[[bin]]\n"
    "name = \"app\"\n"
    "path = \"src/main.styio\"\n"
  );
  WriteFile(root / "src/main.styio", ">_(\"app\")\n");

  testing::internal::CaptureStdout();
  EXPECT_EQ(
    pafio::RunCli({"--json", "check", "--manifest-path", manifest_path.string(), "--dry-run"}),
    pafio::kExitSuccess
  );
  const json payload = json::parse(testing::internal::GetCapturedStdout());

  EXPECT_EQ(payload.at("action").get<std::string>(), "check");
  EXPECT_EQ(payload.at("command").get<std::string>(), "check");
  EXPECT_EQ(payload.at("status").get<std::string>(), "planned");
  EXPECT_EQ(payload.at("sync").at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(payload.at("sync").at("packages").get<size_t>(), 1U);
}
