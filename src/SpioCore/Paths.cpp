#include "SpioCore/Paths.hpp"

#include "SpioCore/Errors.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace spio
{

fs::path ProjectRoot()
{
  return fs::path(SPIO_PROJECT_ROOT);
}

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

fs::path ProjectStateRootForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(manifest_path).parent_path() / ".spio");
}

fs::path ProjectVendorRootForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(ProjectStateRootForManifest(manifest_path) / "vendor");
}

fs::path ProjectToolchainPinPathForManifest(const fs::path &manifest_path)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(manifest_path).parent_path() / "spio-toolchain.toml");
}

std::optional<fs::path> FindProjectToolchainPinPath(const fs::path &manifest_path)
{
  fs::path current = CanonicalAbsolutePath(manifest_path).parent_path();
  while (true)
  {
    const fs::path candidate = CanonicalAbsolutePath(current / "spio-toolchain.toml");
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

std::optional<fs::path> ResolveOptionalSpioHome()
{
  if (const char *explicit_home = std::getenv("SPIO_HOME"); explicit_home != nullptr && explicit_home[0] != '\0')
  {
    return CanonicalAbsolutePath(explicit_home);
  }
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
  {
    return CanonicalAbsolutePath(fs::path(home) / ".spio");
  }
  return std::nullopt;
}

fs::path ResolveSpioHome()
{
  const std::optional<fs::path> spio_home = ResolveOptionalSpioHome();
  if (!spio_home.has_value())
  {
    throw CacheError("unable to resolve SPIO_HOME: set SPIO_HOME or HOME");
  }
  return *spio_home;
}

fs::path ManagedToolsRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(spio_home) / "tools");
}

fs::path ManagedStyioRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(ManagedToolsRoot(spio_home) / "styio");
}

fs::path ManagedStyioInstallRoot(const fs::path &spio_home, const std::string &channel, const std::string &compiler_version)
{
  return CanonicalAbsolutePath(ManagedStyioRoot(spio_home) / channel / compiler_version);
}

fs::path ManagedStyioCurrentRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(ManagedStyioRoot(spio_home) / "current");
}

fs::path ManagedStyioBinaryPath(const fs::path &root)
{
  return CanonicalAbsolutePath(root / "bin" / "styio");
}

fs::path ManagedStyioMetadataPath(const fs::path &root)
{
  return CanonicalAbsolutePath(root / "install.json");
}

fs::path RegistryCacheRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(spio_home) / "registry");
}

fs::path RegistryIndexCacheRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(spio_home) / "index");
}

fs::path RegistryBlobCacheRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(spio_home) / "blobs" / "sha256");
}

fs::path RegistryCheckoutRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(spio_home) / "checkouts");
}

fs::path ServerStateRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(CanonicalAbsolutePath(spio_home) / "server");
}

fs::path RegistryServerRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(ServerStateRoot(spio_home) / "registry");
}

fs::path RegistryPublishProfileRoot(const fs::path &spio_home)
{
  return CanonicalAbsolutePath(RegistryServerRoot(spio_home) / "publish-profiles");
}

fs::path RegistryPublishProfilePath(const fs::path &spio_home, const std::string &profile_name)
{
  return CanonicalAbsolutePath(RegistryPublishProfileRoot(spio_home) / (profile_name + ".toml"));
}

}  // namespace spio
