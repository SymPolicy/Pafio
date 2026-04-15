#include "SpioSecurity/RegistrySecurity.hpp"

#include "SpioCore/Errors.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

enum class SecurityHandlerResult
{
  kContinue,
  kResolved,
};

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

using ReadSecurityHandler =
    SecurityHandlerResult (*)(const spio::RegistryReadSecurityRequest &request, spio::RegistryReadSecurityDecision &decision);
using WriteSecurityHandler =
    SecurityHandlerResult (*)(const spio::RegistryWriteSecurityRequest &request, spio::RegistryWriteSecurityDecision &decision);

template <typename Request, typename Decision, typename Handler, size_t N>
Decision RunSecurityChain(
    const Request &request,
    Decision decision,
    const std::array<Handler, N> &handlers,
    const char *chain_name)
{
  for (const Handler handler : handlers)
  {
    if (handler(request, decision) == SecurityHandlerResult::kResolved)
    {
      return decision;
    }
  }

  throw std::logic_error(std::string(chain_name) + " registry security chain did not resolve");
}

SecurityHandlerResult NormalizeReadRegistryRoot(
    const spio::RegistryReadSecurityRequest &request,
    spio::RegistryReadSecurityDecision &decision)
{
  decision.registry_root = NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ValidateReadRegistryRootScheme(
    const spio::RegistryReadSecurityRequest &request,
    spio::RegistryReadSecurityDecision &decision)
{
  if (!IsFileRegistryRoot(decision.registry_root) && !IsHttpRegistryRoot(decision.registry_root))
  {
    throw spio::FetchError("registry root must use file://, http://, or https://: " + request.registry_root);
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ResolvePublicDefaultReadAccess(
    const spio::RegistryReadSecurityRequest &request,
    spio::RegistryReadSecurityDecision &decision)
{
  (void) request;
  decision.request_headers.clear();
  decision.provider_name = "public-default";
  return SecurityHandlerResult::kResolved;
}

SecurityHandlerResult NormalizeWriteRegistryRoot(
    const spio::RegistryWriteSecurityRequest &request,
    spio::RegistryWriteSecurityDecision &decision)
{
  decision.registry_root = NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ValidateWriteRegistryRootScheme(
    const spio::RegistryWriteSecurityRequest &request,
    spio::RegistryWriteSecurityDecision &decision)
{
  if (!IsHttpRegistryRoot(decision.registry_root))
  {
    throw spio::PublishError("remote registry publish requires an http:// or https:// registry root: " + request.registry_root);
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult RejectOpenSourceWriteSecurityHooks(
    const spio::RegistryWriteSecurityRequest &request,
    spio::RegistryWriteSecurityDecision &decision)
{
  (void) decision;
  if (request.profile_name.has_value() || request.policy_file.has_value() || !request.explicit_request_headers.empty())
  {
    throw spio::PublishError(
        "registry write security hooks require a private module under src-private/SpioSecurity and are not available "
        "in the open-source core");
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ResolvePublicDefaultWriteAccess(
    const spio::RegistryWriteSecurityRequest &request,
    spio::RegistryWriteSecurityDecision &decision)
{
  (void) request;
  decision.request_headers.clear();
  decision.provider_name = "public-default";
  decision.mode = "anonymous";
  decision.profile_name = std::nullopt;
  return SecurityHandlerResult::kResolved;
}

const std::array<ReadSecurityHandler, 3> kDefaultReadSecurityHandlers = {
    NormalizeReadRegistryRoot,
    ValidateReadRegistryRootScheme,
    ResolvePublicDefaultReadAccess,
};

const std::array<WriteSecurityHandler, 4> kDefaultWriteSecurityHandlers = {
    NormalizeWriteRegistryRoot,
    ValidateWriteRegistryRootScheme,
    RejectOpenSourceWriteSecurityHooks,
    ResolvePublicDefaultWriteAccess,
};

spio::RegistryReadSecurityResolver &ReadSecurityResolverSlot()
{
  static spio::RegistryReadSecurityResolver resolver = spio::ResolveDefaultRegistryReadSecurity;
  return resolver;
}

spio::RegistryWriteSecurityResolver &WriteSecurityResolverSlot()
{
  static spio::RegistryWriteSecurityResolver resolver = spio::ResolveDefaultRegistryWriteSecurity;
  return resolver;
}

}  // namespace

namespace spio
{

RegistryReadSecurityDecision ResolveDefaultRegistryReadSecurity(const RegistryReadSecurityRequest &request)
{
  return RunSecurityChain(
      request,
      RegistryReadSecurityDecision{},
      kDefaultReadSecurityHandlers,
      "default read");
}

RegistryWriteSecurityDecision ResolveDefaultRegistryWriteSecurity(const RegistryWriteSecurityRequest &request)
{
  return RunSecurityChain(
      request,
      RegistryWriteSecurityDecision{},
      kDefaultWriteSecurityHandlers,
      "default write");
}

RegistryReadSecurityResolver RegisterRegistryReadSecurityResolver(RegistryReadSecurityResolver resolver)
{
  RegistryReadSecurityResolver &slot = ReadSecurityResolverSlot();
  RegistryReadSecurityResolver previous = slot;
  slot = resolver != nullptr ? resolver : ResolveDefaultRegistryReadSecurity;
  return previous;
}

RegistryWriteSecurityResolver RegisterRegistryWriteSecurityResolver(RegistryWriteSecurityResolver resolver)
{
  RegistryWriteSecurityResolver &slot = WriteSecurityResolverSlot();
  RegistryWriteSecurityResolver previous = slot;
  slot = resolver != nullptr ? resolver : ResolveDefaultRegistryWriteSecurity;
  return previous;
}

RegistryReadSecurityDecision ResolveRegistryReadSecurity(const RegistryReadSecurityRequest &request)
{
  return ReadSecurityResolverSlot()(request);
}

RegistryWriteSecurityDecision ResolveRegistryWriteSecurity(const RegistryWriteSecurityRequest &request)
{
  return WriteSecurityResolverSlot()(request);
}

}  // namespace spio
