#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace spio
{

std::filesystem::path ProjectRoot();
std::filesystem::path CanonicalAbsolutePath(const std::filesystem::path &path);
std::filesystem::path ProjectStateRootForManifest(const std::filesystem::path &manifest_path);
std::filesystem::path ProjectVendorRootForManifest(const std::filesystem::path &manifest_path);
std::filesystem::path ProjectToolchainPinPathForManifest(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> FindProjectToolchainPinPath(const std::filesystem::path &manifest_path);
std::optional<std::filesystem::path> ResolveOptionalSpioHome();
std::filesystem::path ResolveSpioHome();
std::filesystem::path ManagedToolsRoot(const std::filesystem::path &spio_home);
std::filesystem::path ManagedStyioRoot(const std::filesystem::path &spio_home);
std::filesystem::path ManagedStyioInstallRoot(
    const std::filesystem::path &spio_home,
    const std::string &channel,
    const std::string &compiler_version);
std::filesystem::path ManagedStyioCurrentRoot(const std::filesystem::path &spio_home);
std::filesystem::path ManagedStyioBinaryPath(const std::filesystem::path &root);
std::filesystem::path ManagedStyioMetadataPath(const std::filesystem::path &root);
std::filesystem::path GitCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path GitMirrorCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path GitCheckoutCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryIndexCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryBlobCacheRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryCheckoutRoot(const std::filesystem::path &spio_home);
std::filesystem::path ServerStateRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryServerRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryPublishProfileRoot(const std::filesystem::path &spio_home);
std::filesystem::path RegistryPublishProfilePath(const std::filesystem::path &spio_home, const std::string &profile_name);

}  // namespace spio
