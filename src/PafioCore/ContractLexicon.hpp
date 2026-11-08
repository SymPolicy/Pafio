#pragma once

#include <string>
#include <string_view>

namespace pafio
{

// Lexical rules shared by pafio.toml, pafio.lock, dependency commands, and
// registry validation: package name (namespace/name), exact semver (x.y.z),
// and lowercase sha256 digests. Single authority — callers throw or test via
// these, never via private regex copies.
bool IsPackageName(const std::string &name);
bool IsExactSemver(const std::string &version);
bool IsSha256Digest(const std::string &digest);

// Git dependency revisions are immutable full commit object ids (40 lowercase
// hex). A movable branch or tag could resolve to different code on different
// machines while leaving manifest and lock bytes unchanged; commit ids keep
// the recorded rev identical to the materialized code.
bool IsGitCommitId(const std::string &rev);

// A URL carrying userinfo (scheme://user[:pass]@host) embeds credentials in
// the source string, where they leak into manifests, locks, process argv,
// persisted trust state, and diagnostics. Authentication must come from a
// credential provider, never from a URL.
bool UrlHasUserinfo(const std::string &value);

// Registry roots are cache keys and trust inputs: one normalization (strip
// trailing '/') shared by the client, verifier, resolver, and publish path.
std::string NormalizeRegistryRoot(std::string value);

// The short name is the segment after '/' in a validated namespace/name.
std::string PackageShortName(const std::string &package_name);

void RequirePackageName(const std::string &name, std::string_view context);
void RequireExactSemver(const std::string &version, std::string_view context);
void RequireSha256Digest(const std::string &digest, std::string_view context);
void RequireGitCommitId(const std::string &rev, std::string_view context);

}  // namespace pafio
