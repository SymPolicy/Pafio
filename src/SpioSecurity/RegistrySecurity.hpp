#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct RegistryReadSecurityRequest
{
  std::string registry_root;
  bool offline = false;
};

struct RegistryReadSecurityDecision
{
  std::string registry_root;
  std::vector<std::string> request_headers;
  std::string provider_name = "public-default";
};

struct RegistryWriteSecurityRequest
{
  std::string registry_root;
  std::optional<std::string> profile_name;
  std::optional<std::filesystem::path> policy_file;
  std::vector<std::string> explicit_request_headers;
};

struct RegistryWriteSecurityDecision
{
  std::string registry_root;
  std::vector<std::string> request_headers;
  std::string provider_name = "public-default";
  std::string mode = "anonymous";
  std::optional<std::string> profile_name;
};

struct RegistryPackageNameParts
{
  std::string namespace_name;
  std::string short_name;
};

using RegistryReadSecurityResolver = RegistryReadSecurityDecision (*)(const RegistryReadSecurityRequest &request);
using RegistryWriteSecurityResolver = RegistryWriteSecurityDecision (*)(const RegistryWriteSecurityRequest &request);

RegistryPackageNameParts SplitRegistryPackageName(const std::string &package_name, const std::string &context);
std::filesystem::path NormalizeRegistryRelativePath(const std::string &relative_path, const std::string &context);
bool IsRegistrySha256Digest(const std::string &value);

RegistryReadSecurityDecision ResolveDefaultRegistryReadSecurity(const RegistryReadSecurityRequest &request);
RegistryWriteSecurityDecision ResolveDefaultRegistryWriteSecurity(const RegistryWriteSecurityRequest &request);

RegistryReadSecurityResolver RegisterRegistryReadSecurityResolver(RegistryReadSecurityResolver resolver);
RegistryWriteSecurityResolver RegisterRegistryWriteSecurityResolver(RegistryWriteSecurityResolver resolver);

RegistryReadSecurityDecision ResolveRegistryReadSecurity(const RegistryReadSecurityRequest &request);
RegistryWriteSecurityDecision ResolveRegistryWriteSecurity(const RegistryWriteSecurityRequest &request);

void ValidateRegistryPackageIdentity(const std::string &package_name);
std::filesystem::path NormalizeRegistryObjectPath(const std::string &relative_path, const std::string &context);

}  // namespace spio
