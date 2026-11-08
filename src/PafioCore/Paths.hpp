#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pafio
{

std::filesystem::path ProjectRoot();
std::filesystem::path CanonicalAbsolutePath(const std::filesystem::path &path);
std::filesystem::path ProjectStateRootForManifest(const std::filesystem::path &manifest_path);
std::filesystem::path ProjectVendorRootForManifest(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> ResolveOptionalPafioHome();
std::filesystem::path ResolvePafioHome();
std::filesystem::path RegistryCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryTrustRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryTrustStorePath(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryIndexCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryBlobCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryCheckoutRoot(const std::filesystem::path &pafio_home);
std::filesystem::path StaticRegistryKeyRoot(const std::filesystem::path &pafio_home);

}  // namespace pafio
