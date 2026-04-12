#include "SpioSecurity/RegistrySecurity.hpp"

#include "SpioCore/Errors.hpp"

#include <utility>

namespace
{

std::string NormalizeRegistryRoot(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

bool IsHttpRegistryRoot(const std::string &value)
{
  return value.starts_with("http://") || value.starts_with("https://");
}

bool IsFileRegistryRoot(const std::string &value)
{
  return value.starts_with("file://");
}

spio::RegistryReadSecurityDecision DefaultResolveRegistryReadSecurity(const spio::RegistryReadSecurityRequest &request)
{
  const std::string normalized_registry_root = NormalizeRegistryRoot(request.registry_root);
  if (!IsFileRegistryRoot(normalized_registry_root) && !IsHttpRegistryRoot(normalized_registry_root))
  {
    throw spio::FetchError("registry root must use file://, http://, or https://: " + request.registry_root);
  }

  return {
      .registry_root = normalized_registry_root,
      .request_headers = {},
      .provider_name = "public-default",
  };
}

spio::RegistryWriteSecurityDecision DefaultResolveRegistryWriteSecurity(const spio::RegistryWriteSecurityRequest &request)
{
  const std::string normalized_registry_root = NormalizeRegistryRoot(request.registry_root);
  if (!IsHttpRegistryRoot(normalized_registry_root))
  {
    throw spio::PublishError("remote registry publish requires an http:// or https:// registry root: " + request.registry_root);
  }

  if (request.profile_name.has_value() || request.policy_file.has_value() || !request.explicit_request_headers.empty())
  {
    throw spio::PublishError(
        "registry write security hooks require a private module under src-private/SpioSecurity and are not available "
        "in the open-source core");
  }

  return {
      .registry_root = normalized_registry_root,
      .request_headers = {},
      .provider_name = "public-default",
      .mode = "anonymous",
      .profile_name = std::nullopt,
  };
}

spio::RegistryReadSecurityResolver g_registry_read_security_resolver = DefaultResolveRegistryReadSecurity;
spio::RegistryWriteSecurityResolver g_registry_write_security_resolver = DefaultResolveRegistryWriteSecurity;

}  // namespace

namespace spio
{

RegistryReadSecurityResolver RegisterRegistryReadSecurityResolver(RegistryReadSecurityResolver resolver)
{
  RegistryReadSecurityResolver previous = g_registry_read_security_resolver;
  g_registry_read_security_resolver = resolver != nullptr ? resolver : DefaultResolveRegistryReadSecurity;
  return previous;
}

RegistryWriteSecurityResolver RegisterRegistryWriteSecurityResolver(RegistryWriteSecurityResolver resolver)
{
  RegistryWriteSecurityResolver previous = g_registry_write_security_resolver;
  g_registry_write_security_resolver = resolver != nullptr ? resolver : DefaultResolveRegistryWriteSecurity;
  return previous;
}

RegistryReadSecurityDecision ResolveRegistryReadSecurity(const RegistryReadSecurityRequest &request)
{
  return g_registry_read_security_resolver(request);
}

RegistryWriteSecurityDecision ResolveRegistryWriteSecurity(const RegistryWriteSecurityRequest &request)
{
  return g_registry_write_security_resolver(request);
}

}  // namespace spio
