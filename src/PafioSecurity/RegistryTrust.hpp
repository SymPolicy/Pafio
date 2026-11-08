#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pafio
{

struct RegistryTrustPin
{
  std::string registry_root;
  std::string registry_name;
  std::string root_sha256;
  std::string descriptor_sha256;
  std::string descriptor_source;
  std::string control_plane_base_url;
  std::string issued_at;
  std::string expires;
  // Persisted replay floors: the newest authenticated timestamp/snapshot
  // versions this client has accepted. They only ever increase, so an
  // attacker replaying still-unexpired older metadata is refused.
  int timestamp_floor = 0;
  int snapshot_floor = 0;
};

struct RegistryTrustImportOptions
{
  bool allow_dev_unsigned = false;
};

std::string NormalizeRegistryTrustRoot(std::string value);
RegistryTrustPin ImportRegistryTrustDescriptor(
    const std::filesystem::path &pafio_home,
    const std::string &descriptor_source,
    const RegistryTrustImportOptions &options = {});
std::optional<RegistryTrustPin> ResolveRegistryTrustPin(
    const std::filesystem::path &pafio_home,
    const std::string &registry_root);
std::vector<RegistryTrustPin> LoadRegistryTrustPins(const std::filesystem::path &pafio_home);
nlohmann::json SerializeRegistryTrustPin(const RegistryTrustPin &pin);
// Raise the persisted replay floors for a trusted registry root after a
// successful metadata verification. Floors never decrease, and the update is
// serialized with other trust-store writers under the trust file lock.
void RaiseRegistryTrustFloors(
    const std::filesystem::path &pafio_home,
    const std::string &registry_root,
    int timestamp_version,
    int snapshot_version);

}  // namespace pafio
