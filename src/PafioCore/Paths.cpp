#include "PafioCore/Paths.hpp"

#include "PafioCore/Errors.hpp"

#include <cstdlib>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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
#if !defined(_WIN32)
  const struct passwd *entry = ::getpwuid(::getuid());
  if (entry != nullptr && entry->pw_dir != nullptr && entry->pw_dir[0] != '\0')
  {
    return CanonicalAbsolutePath(fs::path(entry->pw_dir) / ".pafio");
  }
#endif
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

fs::path StaticRegistryKeyRoot(const fs::path &pafio_home)
{
  return CanonicalAbsolutePath(RegistryCacheRoot(pafio_home) / "static" / "keys");
}

}  // namespace pafio
