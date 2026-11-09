#include "PafioSecurity/RegistrySecurity.hpp"

#include "PafioCore/Errors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

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

bool IsLoopbackHttpRegistryRoot(const std::string &value)
{
  constexpr std::string_view kScheme = "http://";
  if (!value.starts_with(kScheme))
  {
    return false;
  }
  const std::string authority = std::string(value).substr(kScheme.size(), value.find('/', kScheme.size()) - kScheme.size());
  std::string host = authority;
  if (host.starts_with("["))
  {
    const std::size_t close = host.find(']');
    host = close == std::string::npos ? "" : host.substr(1, close - 1);
    return host == "::1";
  }
  if (const std::size_t colon = host.rfind(':'); colon != std::string::npos)
  {
    host = host.substr(0, colon);
  }
  for (char &ch : host)
  {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (host == "localhost" || host == "localhost.")
  {
    return true;
  }
  // Only a strict dotted IPv4 literal may claim loopback: a registered name
  // that merely shares the 127. prefix resolves through DNS to a remote host.
  // Shorthand or non-decimal inet_aton forms are rejected rather than
  // reinterpreted.
  if (std::count(host.begin(), host.end(), '.') != 3)
  {
    return false;
  }
  std::stringstream stream(host);
  std::string segment;
  int octets = 0;
  while (std::getline(stream, segment, '.'))
  {
    if (octets >= 4 || segment.empty() || segment.size() > 3 ||
        !std::all_of(segment.begin(), segment.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }) ||
        std::stoi(segment) > 255)
    {
      return false;
    }
    if (octets == 0 && std::stoi(segment) != 127)
    {
      return false;
    }
    ++octets;
  }
  return octets == 4;
}

bool RegistryRootHasUserinfo(const std::string &value)
{
  const std::size_t scheme_end = value.find("://");
  if (scheme_end == std::string::npos)
  {
    return false;
  }
  const std::size_t authority_start = scheme_end + 3;
  const std::size_t authority_end = value.find('/', authority_start);
  const std::string authority = value.substr(authority_start, authority_end - authority_start);
  return authority.find('@') != std::string::npos;
}

std::vector<std::string> SplitPosixPath(const std::string &value)
{
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, '/'))
  {
    parts.push_back(part);
  }
  return parts;
}

bool IsSafePackageSegment(const std::string &value)
{
  if (value.empty())
  {
    return false;
  }
  const auto is_lower_or_digit = [](const unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || std::isdigit(ch) != 0;
  };
  if (!is_lower_or_digit(static_cast<unsigned char>(value.front())))
  {
    return false;
  }
  for (const unsigned char ch : value)
  {
    if (!is_lower_or_digit(ch) && ch != '-' && ch != '_')
    {
      return false;
    }
  }
  return true;
}

using ReadSecurityHandler =
    SecurityHandlerResult (*)(const pafio::RegistryReadSecurityRequest &request, pafio::RegistryReadSecurityDecision &decision);
using WriteSecurityHandler =
    SecurityHandlerResult (*)(const pafio::RegistryWriteSecurityRequest &request, pafio::RegistryWriteSecurityDecision &decision);

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
    const pafio::RegistryReadSecurityRequest &request,
    pafio::RegistryReadSecurityDecision &decision)
{
  decision.registry_root = NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ValidateReadRegistryRootScheme(
    const pafio::RegistryReadSecurityRequest &request,
    pafio::RegistryReadSecurityDecision &decision)
{
  if (!IsFileRegistryRoot(decision.registry_root) && !IsHttpRegistryRoot(decision.registry_root))
  {
    throw pafio::FetchError("registry root must use file://, http://, or https://: " + request.registry_root);
  }
  if (RegistryRootHasUserinfo(decision.registry_root))
  {
    throw pafio::FetchError(
        "registry root must not embed credentials in the URL userinfo; configure authentication separately");
  }
  if (decision.registry_root.starts_with("http://") && !IsLoopbackHttpRegistryRoot(decision.registry_root))
  {
    throw pafio::FetchError(
        "remote registry reads require https://; http:// registry roots are limited to loopback development registries");
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ResolvePublicDefaultReadAccess(
    const pafio::RegistryReadSecurityRequest &request,
    pafio::RegistryReadSecurityDecision &decision)
{
  (void) request;
  decision.request_headers.clear();
  decision.provider_name = "public-default";
  return SecurityHandlerResult::kResolved;
}

SecurityHandlerResult NormalizeWriteRegistryRoot(
    const pafio::RegistryWriteSecurityRequest &request,
    pafio::RegistryWriteSecurityDecision &decision)
{
  decision.registry_root = NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ValidateWriteRegistryRootScheme(
    const pafio::RegistryWriteSecurityRequest &request,
    pafio::RegistryWriteSecurityDecision &decision)
{
  if (!IsHttpRegistryRoot(decision.registry_root))
  {
    throw pafio::PublishError("remote registry publish requires an http:// or https:// registry root: " + request.registry_root);
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult RejectOpenSourceWriteSecurityHooks(
    const pafio::RegistryWriteSecurityRequest &request,
    pafio::RegistryWriteSecurityDecision &decision)
{
  (void) decision;
  if (request.profile_name.has_value() || request.policy_file.has_value() || !request.explicit_request_headers.empty())
  {
    throw pafio::PublishError(
        "registry write security hooks require a private module under src-private/PafioSecurity and are not available "
        "in the open-source core");
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult ResolvePublicDefaultWriteAccess(
    const pafio::RegistryWriteSecurityRequest &request,
    pafio::RegistryWriteSecurityDecision &decision)
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

pafio::RegistryReadSecurityResolver &ReadSecurityResolverSlot()
{
  static pafio::RegistryReadSecurityResolver resolver = pafio::ResolveDefaultRegistryReadSecurity;
  return resolver;
}

pafio::RegistryWriteSecurityResolver &WriteSecurityResolverSlot()
{
  static pafio::RegistryWriteSecurityResolver resolver = pafio::ResolveDefaultRegistryWriteSecurity;
  return resolver;
}

}  // namespace

namespace pafio
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

RegistryPackageNameParts SplitRegistryPackageName(const std::string &package_name, const std::string &context)
{
  const size_t slash = package_name.find('/');
  if (slash == std::string::npos || slash != package_name.rfind('/'))
  {
    throw FetchError(context + " must match namespace/name: " + package_name);
  }
  RegistryPackageNameParts parts{
      .namespace_name = package_name.substr(0U, slash),
      .short_name = package_name.substr(slash + 1U),
  };
  if (!IsSafePackageSegment(parts.namespace_name) || !IsSafePackageSegment(parts.short_name))
  {
    throw FetchError(context + " must match namespace/name: " + package_name);
  }
  return parts;
}

void ValidateRegistryPackageIdentity(const std::string &package_name)
{
  (void) SplitRegistryPackageName(package_name, "registry package name");
}

std::filesystem::path NormalizeRegistryObjectPath(const std::string &relative_path, const std::string &context)
{
  if (relative_path.empty() || relative_path.ends_with('/'))
  {
    throw FetchError(context + " must be a non-empty POSIX-relative path: " + relative_path);
  }
  if (relative_path.find('\\') != std::string::npos || relative_path.starts_with('/'))
  {
    throw FetchError(context + " must be a POSIX-relative path inside the registry root: " + relative_path);
  }
  for (const unsigned char ch : relative_path)
  {
    if (ch < 32U)
    {
      throw FetchError(context + " must not contain control characters: " + relative_path);
    }
  }
  const fs::path path(relative_path);
  if (path.is_absolute())
  {
    throw FetchError(context + " must be relative: " + relative_path);
  }
  for (const std::string &part : SplitPosixPath(relative_path))
  {
    if (part.empty() || part == "." || part == "..")
    {
      throw FetchError(context + " must be a canonical POSIX-relative path inside the registry root: " + relative_path);
    }
  }
  const fs::path normalized = path.lexically_normal();
  const std::string text = normalized.generic_string();
  if (text.empty() || text == "." || text == ".." || text.starts_with("../"))
  {
    throw FetchError(context + " escapes the registry root: " + relative_path);
  }
  return normalized;
}

std::filesystem::path NormalizeRegistryRelativePath(const std::string &relative_path, const std::string &context)
{
  return NormalizeRegistryObjectPath(relative_path, context);
}

bool IsRegistrySha256Digest(const std::string &value)
{
  if (value.size() != 64U)
  {
    return false;
  }
  for (const unsigned char ch : value)
  {
    if (!std::isdigit(ch) && (ch < 'a' || ch > 'f'))
    {
      return false;
    }
  }
  return true;
}

}  // namespace pafio
