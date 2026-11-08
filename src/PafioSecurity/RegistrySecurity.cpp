#include "PafioSecurity/RegistrySecurity.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"

namespace fs = std::filesystem;

namespace
{

enum class SecurityHandlerResult
{
  kContinue,
  kResolved,
};

bool
IsHttpRegistryRoot(const std::string &value) {
  return value.starts_with("http://") || value.starts_with("https://");
}

bool
IsFileRegistryRoot(const std::string &value) {
  return value.starts_with("file://");
}

bool
IsLoopbackHttpRegistryRoot(const std::string &value) {
  constexpr std::string_view kScheme = "http://";
  if (!value.starts_with(kScheme)) {
    return false;
  }
  const std::string authority = std::string(value).substr(
      kScheme.size(), value.find('/', kScheme.size()) - kScheme.size());
  std::string host = authority;
  // Credentials in userinfo are rejected before this handler runs, so the
  // authority splits cleanly into host and optional port.
  if (host.starts_with("[")) {
    const std::size_t close = host.find(']');
    host = close == std::string::npos ? "" : host.substr(1, close - 1);
    return host == "::1";
  }
  if (const std::size_t colon = host.rfind(':');
      colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  for (char &ch : host) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (host == "localhost" || host == "localhost.") {
    return true;
  }
  // Only a strict dotted IPv4 literal may claim loopback: a registered
  // name that merely shares the 127. prefix resolves through DNS to a
  // remote host. Shorthand or non-decimal inet_aton forms are rejected
  // rather than reinterpreted.
  if (std::count(host.begin(), host.end(), '.') != 3) {
    return false;
  }
  std::stringstream stream(host);
  std::string segment;
  int octets = 0;
  while (std::getline(stream, segment, '.')) {
    if (octets >= 4 || segment.empty() || segment.size() > 3 ||
        !std::all_of(segment.begin(), segment.end(), [](unsigned char ch) {
          return std::isdigit(ch) != 0;
        }) ||
        std::stoi(segment) > 255) {
      return false;
    }
    if (octets == 0 && std::stoi(segment) != 127) {
      return false;
    }
    ++octets;
  }
  return octets == 4;
}

std::vector<std::string>
SplitPosixPath(const std::string &value) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, '/')) {
    parts.push_back(part);
  }
  return parts;
}

bool
IsSafePackageSegment(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  const auto is_lower_or_digit = [](const unsigned char ch)
  {
    return (ch >= 'a' && ch <= 'z') || std::isdigit(ch) != 0;
  };
  if (!is_lower_or_digit(static_cast<unsigned char>(value.front()))) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (!is_lower_or_digit(ch) && ch != '-' && ch != '_') {
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
Decision
RunSecurityChain(
  const Request &request,
  Decision decision,
  const std::array<Handler, N> &handlers,
  const char *chain_name
) {
  for (const Handler handler : handlers) {
    if (handler(request, decision) == SecurityHandlerResult::kResolved) {
      return decision;
    }
  }

  throw std::logic_error(std::string(chain_name) + " registry security chain did not resolve");
}

SecurityHandlerResult
NormalizeReadRegistryRoot(
  const pafio::RegistryReadSecurityRequest &request,
  pafio::RegistryReadSecurityDecision &decision
) {
  decision.registry_root = pafio::NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult
ValidateReadRegistryRootScheme(
  const pafio::RegistryReadSecurityRequest &request,
  pafio::RegistryReadSecurityDecision &decision
) {
  if (!IsFileRegistryRoot(decision.registry_root) && !IsHttpRegistryRoot(decision.registry_root)) {
    throw pafio::FetchError("registry root must use file://, http://, or https://: " + request.registry_root);
  }
  if (pafio::UrlHasUserinfo(decision.registry_root)) {
    throw pafio::FetchError(
        "registry root must not embed credentials in the URL userinfo; configure authentication separately");
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult
ResolvePublicDefaultReadAccess(
  const pafio::RegistryReadSecurityRequest &request,
  pafio::RegistryReadSecurityDecision &decision
) {
  (void)request;
  decision.request_headers.clear();
  decision.provider_name = "public-default";
  return SecurityHandlerResult::kResolved;
}

SecurityHandlerResult
NormalizeWriteRegistryRoot(
  const pafio::RegistryWriteSecurityRequest &request,
  pafio::RegistryWriteSecurityDecision &decision
) {
  decision.registry_root = pafio::NormalizeRegistryRoot(request.registry_root);
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult
ValidateWriteRegistryRootScheme(
  const pafio::RegistryWriteSecurityRequest &request,
  pafio::RegistryWriteSecurityDecision &decision
) {
  if (!IsHttpRegistryRoot(decision.registry_root)) {
    throw pafio::PublishError("remote registry publish requires an https:// registry root: " + request.registry_root);
  }
  if (pafio::UrlHasUserinfo(decision.registry_root)) {
    throw pafio::PublishError(
        "registry root must not embed credentials in the URL userinfo; configure authentication separately");
  }
  // Plaintext publish exposes unpublished package contents and headers to
  // network observers, so the opt-in stays bound to loopback registries;
  // a remote http:// endpoint cannot be enabled by the flag.
  if (decision.registry_root.starts_with("http://")) {
    if (!request.allow_plaintext_http) {
      throw pafio::PublishError(
          "remote registry publish requires https://; use --allow-plaintext-http only for local development registries");
    }
    if (!IsLoopbackHttpRegistryRoot(decision.registry_root)) {
      throw pafio::PublishError(
          "--allow-plaintext-http is limited to loopback registries; use https:// for remote registries");
    }
  }
  return SecurityHandlerResult::kContinue;
}

SecurityHandlerResult
ResolvePublicDefaultWriteAccess(
  const pafio::RegistryWriteSecurityRequest &request,
  pafio::RegistryWriteSecurityDecision &decision
) {
  (void)request;
  decision.request_headers.clear();
  decision.provider_name = "public-default";
  decision.mode = "anonymous";
  return SecurityHandlerResult::kResolved;
}

const std::array<ReadSecurityHandler, 3> kDefaultReadSecurityHandlers = {
  NormalizeReadRegistryRoot,
  ValidateReadRegistryRootScheme,
  ResolvePublicDefaultReadAccess,
};

const std::array<WriteSecurityHandler, 3> kDefaultWriteSecurityHandlers = {
  NormalizeWriteRegistryRoot,
  ValidateWriteRegistryRootScheme,
  ResolvePublicDefaultWriteAccess,
};

std::atomic<pafio::RegistryReadSecurityResolver> &
ReadSecurityResolverSlot() {
  static std::atomic<pafio::RegistryReadSecurityResolver> resolver{
    pafio::ResolveDefaultRegistryReadSecurity
  };
  return resolver;
}

std::atomic<pafio::RegistryWriteSecurityResolver> &
WriteSecurityResolverSlot() {
  static std::atomic<pafio::RegistryWriteSecurityResolver> resolver{
    pafio::ResolveDefaultRegistryWriteSecurity
  };
  return resolver;
}

}  // namespace

namespace pafio
{

RegistryReadSecurityDecision
ResolveDefaultRegistryReadSecurity(const RegistryReadSecurityRequest &request) {
  return RunSecurityChain(
    request,
    RegistryReadSecurityDecision{},
    kDefaultReadSecurityHandlers,
    "default read"
  );
}

RegistryWriteSecurityDecision
ResolveDefaultRegistryWriteSecurity(const RegistryWriteSecurityRequest &request) {
  return RunSecurityChain(
    request,
    RegistryWriteSecurityDecision{},
    kDefaultWriteSecurityHandlers,
    "default write"
  );
}

RegistryReadSecurityResolver
RegisterRegistryReadSecurityResolver(RegistryReadSecurityResolver resolver) {
  return ReadSecurityResolverSlot().exchange(
    resolver != nullptr ? resolver : ResolveDefaultRegistryReadSecurity,
    std::memory_order_acq_rel
  );
}

RegistryWriteSecurityResolver
RegisterRegistryWriteSecurityResolver(RegistryWriteSecurityResolver resolver) {
  return WriteSecurityResolverSlot().exchange(
    resolver != nullptr ? resolver : ResolveDefaultRegistryWriteSecurity,
    std::memory_order_acq_rel
  );
}

RegistryReadSecurityDecision
ResolveRegistryReadSecurity(const RegistryReadSecurityRequest &request) {
  return ReadSecurityResolverSlot().load(std::memory_order_acquire)(request);
}

RegistryWriteSecurityDecision
ResolveRegistryWriteSecurity(const RegistryWriteSecurityRequest &request) {
  return WriteSecurityResolverSlot().load(std::memory_order_acquire)(request);
}

RegistryPackageNameParts
SplitRegistryPackageName(const std::string &package_name, const std::string &context) {
  const size_t slash = package_name.find('/');
  if (slash == std::string::npos || slash != package_name.rfind('/')) {
    throw FetchError(context + " must match namespace/name: " + package_name);
  }
  RegistryPackageNameParts parts{
    .namespace_name = package_name.substr(0U, slash),
    .short_name = package_name.substr(slash + 1U),
  };
  if (!IsSafePackageSegment(parts.namespace_name) || !IsSafePackageSegment(parts.short_name)) {
    throw FetchError(context + " must match namespace/name: " + package_name);
  }
  return parts;
}

void
ValidateRegistryPackageIdentity(const std::string &package_name) {
  (void)SplitRegistryPackageName(package_name, "registry package name");
}

std::filesystem::path
NormalizeRegistryObjectPath(const std::string &relative_path, const std::string &context) {
  if (relative_path.empty() || relative_path.ends_with('/')) {
    throw FetchError(context + " must be a non-empty POSIX-relative path: " + relative_path);
  }
  if (relative_path.find('\\') != std::string::npos || relative_path.starts_with('/')) {
    throw FetchError(context + " must be a POSIX-relative path inside the registry root: " + relative_path);
  }
  for (const unsigned char ch : relative_path) {
    if (ch < 32U) {
      throw FetchError(context + " must not contain control characters: " + relative_path);
    }
  }
  const fs::path path(relative_path);
  if (path.is_absolute()) {
    throw FetchError(context + " must be relative: " + relative_path);
  }
  for (const std::string &part : SplitPosixPath(relative_path)) {
    if (part.empty() || part == "." || part == "..") {
      throw FetchError(context + " must be a canonical POSIX-relative path inside the registry root: " + relative_path);
    }
  }
  const fs::path normalized = path.lexically_normal();
  const std::string text = normalized.generic_string();
  if (text.empty() || text == "." || text == ".." || text.starts_with("../")) {
    throw FetchError(context + " escapes the registry root: " + relative_path);
  }
  return normalized;
}

std::filesystem::path
NormalizeRegistryRelativePath(const std::string &relative_path, const std::string &context) {
  return NormalizeRegistryObjectPath(relative_path, context);
}

bool
IsRegistrySha256Digest(const std::string &value) {
  return IsSha256Digest(value);
}

}  // namespace pafio
