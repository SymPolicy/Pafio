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
std::filesystem::path ProjectToolchainPinPathForManifest(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> FindProjectToolchainPinPath(const std::filesystem::path &manifest_path);
std::filesystem::path ProjectToolchainStatePathForManifest(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> FindProjectToolchainStatePath(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> ResolveOptionalPafioHome();
std::filesystem::path ResolvePafioHome();
std::filesystem::path ManagedToolsRoot(const std::filesystem::path &pafio_home);
std::filesystem::path ManagedStyioRoot(const std::filesystem::path &pafio_home);
std::filesystem::path ManagedStyioInstallRoot(
    const std::filesystem::path &pafio_home,
    const std::string &channel,
    const std::string &compiler_version);
std::filesystem::path ManagedStyioCurrentRoot(const std::filesystem::path &pafio_home);
std::filesystem::path ManagedStyioBinaryPath(const std::filesystem::path &root);
std::filesystem::path ManagedStyioMetadataPath(const std::filesystem::path &root);
std::filesystem::path SourceStateRoot(const std::filesystem::path &pafio_home);
std::filesystem::path SourceCheckoutRoot(
    const std::filesystem::path &pafio_home,
    const std::string &channel,
    const std::string &identity);
std::filesystem::path SourceToolchainBuildRoot(
    const std::filesystem::path &pafio_home,
    const std::string &channel,
    const std::string &identity,
    const std::string &build_mode);
std::filesystem::path RegistryCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryTrustRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryTrustStorePath(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryIndexCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryBlobCacheRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryCheckoutRoot(const std::filesystem::path &pafio_home);
std::filesystem::path ServerStateRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryServerRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryPublishProfileRoot(const std::filesystem::path &pafio_home);
std::filesystem::path RegistryPublishProfilePath(const std::filesystem::path &pafio_home, const std::string &profile_name);

}  // namespace pafio
