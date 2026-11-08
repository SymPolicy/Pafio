#include "PafioCore/ContractLexicon.hpp"

#include <regex>

#include "PafioCore/Errors.hpp"

namespace pafio
{

namespace
{

const std::regex &
PackageNamePattern() {
  static const std::regex pattern("^[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*$");
  return pattern;
}

const std::regex &
ExactSemverPattern() {
  static const std::regex pattern("^\\d+\\.\\d+\\.\\d+$");
  return pattern;
}

const std::regex &
Sha256DigestPattern() {
  static const std::regex pattern("^[0-9a-f]{64}$");
  return pattern;
}

const std::regex &
GitCommitIdPattern() {
  static const std::regex pattern("^[0-9a-f]{40}$");
  return pattern;
}

}  // namespace

bool
IsPackageName(const std::string &name) {
  return std::regex_match(name, PackageNamePattern());
}

bool
IsExactSemver(const std::string &version) {
  return std::regex_match(version, ExactSemverPattern());
}

bool
IsSha256Digest(const std::string &digest) {
  return std::regex_match(digest, Sha256DigestPattern());
}

bool
IsGitCommitId(const std::string &rev) {
  return std::regex_match(rev, GitCommitIdPattern());
}

bool
UrlHasUserinfo(const std::string &value) {
  const std::size_t scheme = value.find("://");
  if (scheme == std::string::npos) {
    return false;
  }
  const std::size_t authority_start = scheme + 3;
  const std::size_t authority_end = value.find('/', authority_start);
  const std::string_view authority =
      authority_end == std::string::npos
          ? std::string_view(value).substr(authority_start)
          : std::string_view(value).substr(authority_start, authority_end - authority_start);
  return authority.find('@') != std::string_view::npos;
}

std::string
NormalizeRegistryRoot(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string
PackageShortName(const std::string &package_name) {
  RequirePackageName(package_name, "package.name");
  return package_name.substr(package_name.find('/') + 1U);
}

void
RequirePackageName(const std::string &name, std::string_view context) {
  if (!IsPackageName(name)) {
    throw ValidationError(std::string(context) + " must match namespace/name");
  }
}

void
RequireExactSemver(const std::string &version, std::string_view context) {
  if (!IsExactSemver(version)) {
    throw ValidationError(
      std::string(context) +
        " must be exact semver x.y.z (ranges, prerelease, and prefixes are not supported in single-version-v1)"
    );
  }
}

void
RequireSha256Digest(const std::string &digest, std::string_view context) {
  if (!IsSha256Digest(digest)) {
    throw ValidationError(std::string(context) + " must be a lowercase sha256 hex digest");
  }
}

void
RequireGitCommitId(const std::string &rev, std::string_view context) {
  if (!IsGitCommitId(rev)) {
    throw ValidationError(
        std::string(context) + " must be a full immutable git commit id (40 lowercase hex); "
        "branch names and tags are not accepted");
  }
}

}  // namespace pafio
