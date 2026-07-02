#include "PafioCore/Paths.hpp"

#include "PafioCore/Errors.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace pafio
{

fs::path ProjectRoot()
{
  return fs::path(PAFIO_PROJECT_ROOT);
}

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

fs::path ProjectStateRootForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(manifest_path).parent_path() / ".pafio");
}

fs::path ProjectVendorRootForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(ProjectStateRootForManifest(manifest_path) / "vendor");
}

fs::path ProjectToolchainPinPathForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(manifest_path).parent_path() / "pafio-toolchain.toml");
}

fs::path ProjectToolchainStatePathForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(manifest_path).parent_path() / "pafio-toolchain.lock");
}

std::optional<fs::path> FindProjectToolchainPinPath(const fs::path &manifest_path)
{
  fs::path current = CanonicalAbsolutePath(manifest_path).parent_path();
  while (true)
  {
    const fs::path candidate = CanonicalAbsolutePath(current / "pafio-toolchain.toml");
    if (fs::exists(candidate))
    {
      return candidate;
    }

    const fs::path parent = current.parent_path();
    if (parent == current)
    {
      break;
    }
    current = parent;
  }
  return std::nullopt;
}

std::optional<fs::path> FindProjectToolchainStatePath(const fs::path &manifest_path)
{
  fs::path current = CanonicalAbsolutePath(manifest_path).parent_path();
  while (true)
  {
    const fs::path candidate = CanonicalAbsolutePath(current / "pafio-toolchain.lock");
    if (fs::exists(candidate))
    {
      return candidate;
    }

    const fs::path parent = current.parent_path();
    if (parent == current)
    {
      break;
    }
    current = parent;
  }
  return std::nullopt;
}

std::optional<fs::path> ResolveOptionalPafioHome()
{
  if (const char *explicit_home = std::getenv("PAFIO_HOME"); explicit_home != nullptr && explicit_home[0] != '\0')
  {
    return CanonicalAbsolutePath(explicit_home);
  }
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
  {
    return CanonicalAbsolutePath(fs::path(home) / ".pafio");
  }
  return std::nullopt;
}

fs::path ResolvePafioHome()
{
  const std::optional<fs::path> pafio_home = ResolveOptionalPafioHome();
  if (!pafio_home.has_value())
  {
    throw CacheError("unable to resolve PAFIO_HOME: set PAFIO_HOME or HOME");
  }
  return *pafio_home;
}

fs::path ManagedToolsRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(pafio_home) / "tools");
}

fs::path ManagedStyioRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(ManagedToolsRoot(pafio_home) / "styio");
}

fs::path ManagedStyioInstallRoot(const fs::path &pafio_home, const std::string &channel, const std::string &compiler_version)
{
  return CanonicalAbsolutePath(ManagedStyioRoot(pafio_home) / channel / compiler_version);
}

fs::path ManagedStyioCurrentRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(ManagedStyioRoot(pafio_home) / "current");
}

fs::path ManagedStyioBinaryPath(const fs::path &root)
{
  return CanonicalAbsolutePath(root / "bin" / "styio");
}

fs::path ManagedStyioMetadataPath(const fs::path &root)
{
  return CanonicalAbsolutePath(root / "install.json");
}

fs::path SourceStateRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(pafio_home) / "src" / "styio");
}

fs::path SourceCheckoutRoot(const fs::path &pafio_home, const std::string &channel, const std::string &identity)
{
  return CanonicalAbsolutePath(SourceStateRoot(pafio_home) / channel / identity / "source");
}

fs::path SourceToolchainBuildRoot(
    const fs::path &pafio_home,
    const std::string &channel,
    const std::string &identity,
    const std::string &build_mode)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(pafio_home) / "toolchains" / "source" / channel / identity / build_mode);
}

fs::path RegistryCacheRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(pafio_home) / "registry");
}

fs::path RegistryTrustRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(pafio_home) / "trust");
}

fs::path RegistryTrustStorePath(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryTrustRoot(pafio_home) / "registry-trust.json");
}

fs::path RegistryIndexCacheRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(pafio_home) / "index");
}

fs::path RegistryBlobCacheRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(pafio_home) / "blobs" / "sha256");
}

fs::path RegistryCheckoutRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(pafio_home) / "checkouts");
}

fs::path ServerStateRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(pafio_home) / "server");
}

fs::path RegistryServerRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(ServerStateRoot(pafio_home) / "registry");
}

fs::path RegistryPublishProfileRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryServerRoot(pafio_home) / "publish-profiles");
}

fs::path RegistryPublishProfilePath(const fs::path &pafio_home, const std::string &profile_name)
{
  return CanonicalAbsolutePath(RegistryPublishProfileRoot(pafio_home) / (profile_name + ".toml"));
}

}  // namespace pafio
