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
};

std::string NormalizeRegistryTrustRoot(std::string value);
RegistryTrustPin ImportRegistryTrustDescriptor(
    const std::filesystem::path &pafio_home,
    const std::string &descriptor_source);
std::optional<RegistryTrustPin> ResolveRegistryTrustPin(
    const std::filesystem::path &pafio_home,
    const std::string &registry_root);
std::vector<RegistryTrustPin> LoadRegistryTrustPins(const std::filesystem::path &pafio_home);
nlohmann::json SerializeRegistryTrustPin(const RegistryTrustPin &pin);

}  // namespace pafio
