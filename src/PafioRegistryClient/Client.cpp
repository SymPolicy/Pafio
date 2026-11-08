#include "PafioRegistryClient/Client.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "PafioCore/AtomicFile.hpp"
#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/FileLock.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioSecurity/PrescanArchive.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"
#include "PafioSecurity/RegistryTrust.hpp"
#include "PafioSecurity/TufVerifier.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

constexpr size_t kRegistryMetadataMaxBytes = 16U * 1024U * 1024U;
constexpr uintmax_t kRegistryArtifactMaxBytes = 512ULL * 1024ULL * 1024ULL;

enum class RegistryRemoteObjectKind
{
  kMetadata,
  kArtifact,
};

uint64_t
ProcessId() {
#if defined(_WIN32)
  return static_cast<uint64_t>(::_getpid());
#else
  return static_cast<uint64_t>(::getpid());
#endif
}

fs::path
UniqueTemporaryPath(const fs::path &path, const std::string_view label) {
  static std::atomic<uint64_t> sequence{0};
  const uint64_t nonce = static_cast<uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count()
  );
  return path.parent_path() /
    (path.filename().string() + ".tmp." + std::string(label) + "." +
     std::to_string(ProcessId()) + "." + std::to_string(nonce) + "." +
     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
}

bool
IsHttpRegistry(const std::string &registry_root) {
  return registry_root.starts_with("http://") || registry_root.starts_with("https://");
}

bool
IsFileRegistry(const std::string &registry_root) {
  return registry_root.starts_with("file://");
}

fs::path
FileUrlToPath(const std::string &registry_root) {
  return fs::path(registry_root.substr(std::string("file://").size()));
}

std::pair<std::string, std::string>
SplitPackageName(const std::string &package_name) {
  pafio::ValidateRegistryPackageIdentity(package_name);
  const size_t slash = package_name.find('/');
  return {
    package_name.substr(0U, slash),
    package_name.substr(slash + 1U),
  };
}

std::string
ReadTextFile(const fs::path &path, const std::string &context) {
  std::ifstream in(path);
  if (!in) {
    throw pafio::FetchError("failed to open " + context + ": " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

json
ParseJsonObject(const std::string &text, const std::string &context) {
  try {
    json payload = json::parse(text);
    if (!payload.is_object()) {
      throw pafio::FetchError(context + " must be a JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &) {
    throw pafio::FetchError(context + " is not valid JSON");
  }
}

fs::path
NormalizeRelativeRegistryPath(const std::string &relative_path, const std::string &context) {
  return pafio::NormalizeRegistryObjectPath(relative_path, context);
}

struct RegistryConfig
{
  std::string targets_prefix = "trust/targets/";
};

RegistryConfig
ValidateConfig(const json &config, const std::string &context) {
  if (config.value("protocol", "") != "pafio-static-registry" || config.value("protocol_version", 0) != 2) {
    throw pafio::FetchError(context + " does not match the supported registry v2 protocol");
  }

  RegistryConfig parsed;
  if (config.contains("paths")) {
    if (!config["paths"].is_object()) {
      throw pafio::FetchError(context + " paths must be a JSON object");
    }
    parsed.targets_prefix = config["paths"].value("targets_prefix", "trust/targets/");
  }
  if (parsed.targets_prefix.empty()) {
    throw pafio::FetchError(context + " is missing paths.targets_prefix");
  }
  return parsed;
}

struct RegistryPackageMetadata
{
  std::string index_path;
};

RegistryPackageMetadata
ValidateTargetsMetadata(
  const json &payload,
  const std::string &package_name,
  const std::string &package_namespace,
  const std::string &version,
  const std::string &context
) {
  if (!payload.contains("signed") || !payload["signed"].is_object()) {
    throw pafio::FetchError(context + " must contain a signed object");
  }
  const json &signed_payload = payload["signed"];
  if (signed_payload.value("type", "") != "targets") {
    throw pafio::FetchError(context + " signed.type must equal 'targets'");
  }
  if (signed_payload.value("namespace", "") != package_namespace) {
    throw pafio::FetchError(
      context + " namespace mismatch: expected '" + package_namespace + "' but found '" + signed_payload.value("namespace", "") + "'"
    );
  }
  if (!signed_payload.contains("packages") || !signed_payload["packages"].is_object()) {
    throw pafio::FetchError(context + " signed.packages must be a JSON object");
  }
  const auto package_it = signed_payload["packages"].find(package_name);
  if (package_it == signed_payload["packages"].end() || !package_it->is_object()) {
    throw pafio::FetchError("registry v2 targets metadata is missing package: " + package_name);
  }

  const std::string index_path = package_it->value("index_path", "");
  if (index_path.empty()) {
    throw pafio::FetchError("registry v2 targets metadata is missing index_path for " + package_name);
  }
  if (!package_it->contains("releases") || !(*package_it)["releases"].is_object()) {
    throw pafio::FetchError("registry v2 targets metadata is missing releases for " + package_name);
  }
  if ((*package_it)["releases"].find(version) == (*package_it)["releases"].end()) {
    throw pafio::FetchError("registry v2 targets metadata does not contain version " + package_name + "@" + version);
  }

  return {
    .index_path = NormalizeRelativeRegistryPath(index_path, "registry v2 package index_path").generic_string(),
  };
}

struct RegistryEntry
{
  std::string package;
  std::string version;
  std::string sha256;
  std::string artifact_path;
  uintmax_t size_bytes = 0;
};

RegistryEntry
ValidateEntry(const json &entry, const std::string &expected_package, const std::string &expected_version) {
  const std::string package = entry.value("package", "");
  const std::string version = entry.value("version", "");
  if (package != expected_package) {
    throw pafio::FetchError("registry v2 entry package mismatch: expected '" + expected_package + "' but found '" + package + "'");
  }
  if (version != expected_version) {
    throw pafio::FetchError("registry v2 entry version mismatch: expected '" + expected_version + "' but found '" + version + "'");
  }
  if (!entry.contains("source_artifact") || !entry["source_artifact"].is_object()) {
    throw pafio::FetchError("registry v2 entry is missing source_artifact");
  }
  const json &artifact = entry["source_artifact"];
  const std::string sha256 = artifact.value("sha256", "");
  const std::string artifact_path = artifact.value("path", "");
  if (!pafio::IsRegistrySha256Digest(sha256)) {
    throw pafio::FetchError("registry v2 source_artifact is missing a valid sha256 digest");
  }
  if (!artifact.contains("size_bytes") || !artifact["size_bytes"].is_number_unsigned()) {
    throw pafio::FetchError("registry v2 source_artifact is missing a valid size_bytes value");
  }
  NormalizeRelativeRegistryPath(artifact_path, "registry v2 source_artifact path");

  return {
    .package = package,
    .version = version,
    .sha256 = sha256,
    .artifact_path = artifact_path,
    .size_bytes = artifact["size_bytes"].get<uintmax_t>(),
  };
}

std::string
TrimAscii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::string
LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch)
                 {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::string
ReadLastContentType(const fs::path &headers_path) {
  std::ifstream in(headers_path);
  if (!in) {
    throw pafio::FetchError("failed to read registry response headers: " + headers_path.string());
  }
  std::string line;
  std::string media_type;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string lowered = LowerAscii(line);
    constexpr std::string_view prefix = "content-type:";
    if (lowered.starts_with(prefix)) {
      media_type = TrimAscii(line.substr(prefix.size()));
      const size_t semicolon = media_type.find(';');
      if (semicolon != std::string::npos) {
        media_type = media_type.substr(0U, semicolon);
      }
      media_type = LowerAscii(TrimAscii(media_type));
    }
  }
  return media_type;
}

bool
IsAllowedRegistryMediaType(const std::string &media_type, const RegistryRemoteObjectKind kind) {
  if (kind == RegistryRemoteObjectKind::kArtifact) {
    return media_type == "application/octet-stream" || media_type == "application/x-tar" || media_type == "application/tar";
  }
  return media_type == "application/json" || media_type == "application/octet-stream" || media_type == "application/x-ndjson" || media_type == "application/jsonl" || media_type == "text/plain";
}

std::vector<std::string>
CurlReadPolicyArgs(const size_t max_bytes, const RegistryRemoteObjectKind object_kind) {
  std::vector<std::string> args{
    "--connect-timeout",
    "10",
    "--speed-time",
    "10",
    "--speed-limit",
    "1024",
    "--max-filesize",
    std::to_string(max_bytes),
  };
  if (object_kind != RegistryRemoteObjectKind::kArtifact) {
    args.push_back("--max-time");
    args.push_back("30");
  }
  return args;
}

void
FetchUrlToFile(
  const std::string &url,
  const fs::path &path,
  const std::vector<std::string> &request_headers,
  const uintmax_t max_bytes,
  const RegistryRemoteObjectKind object_kind
) {
  fs::create_directories(path.parent_path());
  const fs::path temp_path = UniqueTemporaryPath(path, "download");
  const fs::path headers_path = UniqueTemporaryPath(path, "headers");
  const auto cleanup = [&]() noexcept {
    std::error_code ignored;
    fs::remove(temp_path, ignored);
    fs::remove(headers_path, ignored);
  };
  try {
    // -q isolates the invocation from the ambient curl configuration. Redirect
    // following is only allowed when no provider headers are attached: curl
    // forwards custom -H values to redirect targets, so an authenticated
    // request must not leave the original origin.
    std::vector<std::string> args{"-q", "-fsS", "-D", pafio::ProcessPathString(headers_path)};
    if (request_headers.empty()) {
      args.push_back("-L");
    }
    const std::vector<std::string> policy_args =
      CurlReadPolicyArgs(static_cast<size_t>(max_bytes), object_kind);
    args.insert(args.end(), policy_args.begin(), policy_args.end());
    for (const std::string &header : request_headers) {
      args.push_back("-H");
      args.push_back(header);
    }
    args.push_back("-o");
    args.push_back(pafio::ProcessPathString(temp_path));
    args.push_back(url);
    const pafio::ProcessResult result = pafio::RunProcess<pafio::CacheError>({
      .program = pafio::ResolvedCurlPath(),
      .args = args,
      .search_path = false,
      .timeout = object_kind == RegistryRemoteObjectKind::kArtifact
        ? std::nullopt
        : std::optional<std::chrono::milliseconds>{pafio::kExternalProcessStepTimeout},
      .error_context = "registry process",
    });
    if (result.exit_code != 0) {
      throw pafio::FetchError(
        "failed to fetch registry object '" + url + "': " + pafio::DescribeProcessFailure(result)
      );
    }
    const std::string media_type = ReadLastContentType(headers_path);
    std::error_code ignored;
    fs::remove(headers_path, ignored);
    if (!IsAllowedRegistryMediaType(media_type, object_kind)) {
      throw pafio::FetchError(
        "registry object has unsupported media type '" +
        (media_type.empty() ? "<missing>" : media_type) + "': " + url
      );
    }
    std::error_code size_error;
    const uintmax_t downloaded_size = fs::file_size(temp_path, size_error);
    if (size_error || downloaded_size > max_bytes) {
      throw pafio::FetchError("registry object exceeded response limit '" + url + "'");
    }
    std::error_code rename_error;
    fs::rename(temp_path, path, rename_error);
    if (rename_error) {
      throw pafio::CacheError("failed to finalize registry artifact cache: " + path.string());
    }
  }
  catch (...) {
    cleanup();
    throw;
  }
}

std::string
JoinUrl(const std::string &root, const std::string &relative_path) {
  return pafio::NormalizeRegistryRoot(root) + "/" + NormalizeRelativeRegistryPath(relative_path, "registry object path").generic_string();
}

fs::path
RegistryObjectCachePath(const fs::path &pafio_home, const std::string &registry_root, const std::string &relative_path) {
  return pafio::RegistryIndexCacheRoot(pafio_home) / pafio::Sha256Text(registry_root) / NormalizeRelativeRegistryPath(relative_path, "registry cache object path");
}

fs::path
BlobCachePath(const fs::path &pafio_home, const std::string &sha256) {
  return pafio::RegistryBlobCacheRoot(pafio_home) / sha256.substr(0U, 2U) / sha256.substr(2U, 2U) / (sha256 + ".tar");
}

fs::path
CheckoutPath(const fs::path &pafio_home, const std::string &package_name, const std::string &version, const std::string &sha256) {
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return pafio::RegistryCheckoutRoot(pafio_home) / package_namespace / short_name / version / sha256;
}

std::string
LoadRegistryObjectText(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const std::vector<std::string> &request_headers,
  const std::string &relative_path,
  const std::string &context,
  const bool offline,
  std::map<std::string, std::string> &session_objects
) {
  const fs::path normalized_relative = NormalizeRelativeRegistryPath(relative_path, context);
  const std::string session_key = normalized_relative.generic_string();
  if (const auto cached = session_objects.find(session_key); cached != session_objects.end()) {
    return cached->second;
  }

  std::string text;
  if (IsFileRegistry(registry_root)) {
    text = ReadTextFile(FileUrlToPath(registry_root) / normalized_relative, context);
  }
  else {
    if (!IsHttpRegistry(registry_root)) {
      throw pafio::FetchError("registry root must use file://, http://, or https://: " + registry_root);
    }
    // Online objects are immutable within one CLI run; dedupe across package
    // materializations so each metadata object costs one GET per process.
    static std::map<std::string, std::string> online_object_cache;
    const std::string online_key = registry_root + '\x1f' + session_key;
    if (!offline) {
      if (const auto hit = online_object_cache.find(online_key); hit != online_object_cache.end()) {
        session_objects.emplace(session_key, hit->second);
        return hit->second;
      }
    }
    const fs::path cache_path = RegistryObjectCachePath(pafio_home, registry_root, session_key);
    if (offline) {
      if (!fs::exists(cache_path)) {
        throw pafio::FetchError("offline mode is missing cached " + context + " for " + registry_root);
      }
      text = ReadTextFile(cache_path, "cached " + context);
    }
    else {
      FetchUrlToFile(
        JoinUrl(registry_root, session_key),
        cache_path,
        request_headers,
        kRegistryMetadataMaxBytes,
        RegistryRemoteObjectKind::kMetadata
      );
      text = ReadTextFile(cache_path, "cached " + context);
      online_object_cache.emplace(online_key, text);
    }
  }
  session_objects.emplace(session_key, text);
  return text;
}

RegistryConfig
LoadConfig(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const std::vector<std::string> &request_headers,
  const bool offline,
  std::map<std::string, std::string> &session_objects
) {
  return ValidateConfig(
    ParseJsonObject(
      LoadRegistryObjectText(pafio_home, registry_root, request_headers, "config.json", "registry v2 config", offline, session_objects),
      "registry v2 config"
    ),
    "registry v2 config"
  );
}

RegistryPackageMetadata
LoadPackageMetadata(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const std::vector<std::string> &request_headers,
  const RegistryConfig &config,
  const std::string &package_name,
  const std::string &version,
  const bool offline,
  std::map<std::string, std::string> &session_objects
) {
  const auto [package_namespace, _short_name] = SplitPackageName(package_name);
  return ValidateTargetsMetadata(
    ParseJsonObject(
      LoadRegistryObjectText(
        pafio_home,
        registry_root,
        request_headers,
        config.targets_prefix + package_namespace + ".json",
        "registry v2 targets metadata",
        offline,
        session_objects
      ),
      "registry v2 targets metadata"
    ),
    package_name,
    package_namespace,
    version,
    "registry v2 targets metadata"
  );
}

RegistryEntry
LoadEntry(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const std::vector<std::string> &request_headers,
  const std::string &package_name,
  const std::string &version,
  const RegistryPackageMetadata &metadata,
  const bool offline,
  std::map<std::string, std::string> &session_objects
) {
  const std::string text = LoadRegistryObjectText(
    pafio_home,
    registry_root,
    request_headers,
    metadata.index_path,
    "registry v2 index",
    offline,
    session_objects
  );

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }
    const json entry = ParseJsonObject(line, "registry v2 index record");
    if (entry.value("package", "") == package_name && entry.value("version", "") == version) {
      return ValidateEntry(entry, package_name, version);
    }
  }

  throw pafio::FetchError("registry v2 index does not contain package version: " + package_name + "@" + version);
}

fs::path
DetectSnapshotRoot(const fs::path &checkout_root) {
  if (fs::exists(checkout_root / "pafio.toml")) {
    return checkout_root;
  }

  std::vector<fs::path> subdirectories;
  for (const fs::directory_entry &entry : fs::directory_iterator(checkout_root)) {
    if (entry.is_directory()) {
      subdirectories.push_back(entry.path());
    }
  }

  if (subdirectories.size() == 1U && fs::exists(subdirectories.front() / "pafio.toml")) {
    return subdirectories.front();
  }

  throw pafio::CacheError("registry package snapshot does not contain pafio.toml: " + checkout_root.string());
}

void
ValidateTarListingPaths(const fs::path &blob_cache_path) {
  const pafio::ProcessResult path_listing = pafio::RunProcess<pafio::CacheError>({
    .program = pafio::ResolvedTarPath(),
    .args = {"-tf", pafio::ProcessPathString(blob_cache_path)},
    .search_path = false,
    .timeout = pafio::kExternalProcessStepTimeout,
    .max_stdout_bytes = pafio::kArchiveListingMaxBytes,
    .error_context = "registry process",
  });
  if (path_listing.exit_code != 0) {
    throw pafio::CacheError(
      "failed to list registry artifact '" + blob_cache_path.string() + "': " + pafio::DescribeProcessFailure(path_listing)
    );
  }
  const pafio::ProcessResult verbose_listing = pafio::RunProcess<pafio::CacheError>({
    .program = pafio::ResolvedTarPath(),
    .args = {"-tvf", pafio::ProcessPathString(blob_cache_path)},
    .search_path = false,
    .timeout = pafio::kExternalProcessStepTimeout,
    .max_stdout_bytes = pafio::kArchiveListingMaxBytes,
    .error_context = "registry process",
  });
  if (verbose_listing.exit_code != 0) {
    throw pafio::CacheError(
      "failed to inspect registry artifact '" + blob_cache_path.string() + "': " + pafio::DescribeProcessFailure(verbose_listing)
    );
  }
  const bool truncated = path_listing.stdout_truncated || verbose_listing.stdout_truncated;
  const pafio::ArchivePrescanResult prescan = pafio::PrescanArchiveListing(
    path_listing.stdout_text,
    verbose_listing.stdout_text,
    truncated,
    true
  );
  if (!prescan.ok) {
    throw pafio::CacheError(
      std::string("registry archive prescan failed") + (prescan.error_code.empty() ? "" : " (" + prescan.error_code + ")") + ": " + prescan.error
    );
  }
}

fs::path
DigestLockRoot(const fs::path &pafio_home, const std::string &sha256) {
  return pafio::RegistryCacheRoot(pafio_home) / "locks" / "sha256" /
    sha256.substr(0U, 2U) / sha256.substr(2U, 2U) / sha256;
}

std::string
ReadyMarkerBytes(const std::string &sha256, const uintmax_t size_bytes) {
  return json({
    {"schema_version", 1},
    {"sha256", sha256},
    {"size_bytes", size_bytes},
  }).dump(2) + '\n';
}

struct VerifiedCacheHit
{
  fs::path snapshot_root;
  uintmax_t size_bytes = 0;
};

// Digest of a verified blob is stable within a process while its size and
// mtime are unchanged; memoize to avoid rehashing up to 512 MiB per lookup.
std::string
VerifiedBlobSha256(const fs::path &blob_path, const uintmax_t size_bytes) {
  struct Entry
  {
    uintmax_t size = 0;
    fs::file_time_type mtime{};
    std::string sha256;
  };
  static std::map<std::string, Entry> memo;
  const std::string key = blob_path.lexically_normal().generic_string();
  std::error_code ec;
  const fs::file_time_type mtime = fs::last_write_time(blob_path, ec);
  const auto it = memo.find(key);
  if (!ec && it != memo.end() && it->second.size == size_bytes && it->second.mtime == mtime) {
    return it->second.sha256;
  }
  const std::string digest = pafio::Sha256File(blob_path);
  if (!ec) {
    memo[key] = Entry{.size = size_bytes, .mtime = mtime, .sha256 = digest};
  }
  return digest;
}

std::optional<VerifiedCacheHit>
InspectVerifiedCache(
  const fs::path &pafio_home,
  const std::string &package_name,
  const std::string &version,
  const std::string &sha256,
  const std::optional<uintmax_t> expected_size,
  std::string &failure
) {
  const fs::path blob_path = BlobCachePath(pafio_home, sha256);
  const fs::path checkout_root = CheckoutPath(pafio_home, package_name, version, sha256);
  const fs::path ready_marker = checkout_root / ".pafio-snapshot-ready";

  std::error_code status_error;
  if (!fs::is_regular_file(blob_path, status_error) || status_error) {
    failure = "verified blob is missing";
    return std::nullopt;
  }
  const uintmax_t actual_size = fs::file_size(blob_path, status_error);
  if (status_error) {
    failure = "verified blob size is unreadable";
    return std::nullopt;
  }
  if (expected_size.has_value() && actual_size != *expected_size) {
    failure = "cached registry artifact size mismatch";
    return std::nullopt;
  }
  try {
    if (VerifiedBlobSha256(blob_path, actual_size) != sha256) {
      failure = "cached registry artifact sha256 mismatch";
      return std::nullopt;
    }
  }
  catch (const std::exception &error) {
    failure = std::string("cached registry artifact integrity check failed: ") + error.what();
    return std::nullopt;
  }

  if (!fs::is_regular_file(ready_marker, status_error) || status_error) {
    failure = "verified checkout marker is missing";
    return std::nullopt;
  }
  try {
    const json marker = ParseJsonObject(
      ReadTextFile(ready_marker, "registry checkout marker"),
      "registry checkout marker"
    );
    if (marker.size() != 3U ||
        marker.value("schema_version", 0) != 1 ||
        marker.value("sha256", "") != sha256 ||
        !marker.contains("size_bytes") ||
        !marker["size_bytes"].is_number_unsigned() ||
        marker["size_bytes"].get<uintmax_t>() != actual_size) {
      failure = "verified checkout marker does not match the cached artifact";
      return std::nullopt;
    }
  }
  catch (const std::exception &) {
    failure = "verified checkout marker is invalid";
    return std::nullopt;
  }

  try {
    return VerifiedCacheHit{
      .snapshot_root = DetectSnapshotRoot(checkout_root),
      .size_bytes = actual_size,
    };
  }
  catch (const std::exception &error) {
    failure = error.what();
    return std::nullopt;
  }
}

void
RemoveDigestObject(
  const fs::path &pafio_home,
  const std::string &package_name,
  const std::string &version,
  const std::string &sha256
) {
  std::error_code ignored;
  fs::remove(BlobCachePath(pafio_home, sha256), ignored);
  fs::remove_all(CheckoutPath(pafio_home, package_name, version, sha256), ignored);
}

fs::path
CommitRegistryEntry(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const std::vector<std::string> &request_headers,
  const RegistryEntry &entry,
  const bool offline
) {
  std::string cache_failure;
  if (const std::optional<VerifiedCacheHit> cached = InspectVerifiedCache(
        pafio_home,
        entry.package,
        entry.version,
        entry.sha256,
        entry.size_bytes,
        cache_failure);
      cached.has_value()) {
    return cached->snapshot_root;
  }
  if (offline) {
    throw pafio::CacheError(
      "offline registry cache integrity failure for " + entry.package + "@" +
      entry.version + ": " + cache_failure
    );
  }

  RemoveDigestObject(pafio_home, entry.package, entry.version, entry.sha256);

  const fs::path blob_path = BlobCachePath(pafio_home, entry.sha256);
  const fs::path blob_staging = UniqueTemporaryPath(blob_path, "artifact");
  const fs::path checkout_root = CheckoutPath(pafio_home, entry.package, entry.version, entry.sha256);
  const fs::path checkout_staging = UniqueTemporaryPath(checkout_root, "checkout");
  const fs::path artifact_relative =
    NormalizeRelativeRegistryPath(entry.artifact_path, "registry v2 source artifact path");

  fs::create_directories(blob_path.parent_path());
  fs::create_directories(checkout_root.parent_path());
  try {
    if (IsFileRegistry(registry_root)) {
      const fs::path source_blob = FileUrlToPath(registry_root) / artifact_relative;
      if (!fs::exists(source_blob)) {
        throw pafio::FetchError(
          "registry v2 source artifact not found: " + source_blob.string()
        );
      }
      std::error_code copy_error;
      fs::copy_file(source_blob, blob_staging, fs::copy_options::overwrite_existing, copy_error);
      if (copy_error) {
        throw pafio::CacheError(
          "failed to stage registry v2 source artifact: " + blob_staging.string()
        );
      }
    }
    else {
      FetchUrlToFile(
        JoinUrl(registry_root, artifact_relative.generic_string()),
        blob_staging,
        request_headers,
        kRegistryArtifactMaxBytes,
        RegistryRemoteObjectKind::kArtifact
      );
    }

    std::error_code size_error;
    const uintmax_t actual_size = fs::file_size(blob_staging, size_error);
    if (size_error || actual_size != entry.size_bytes) {
      throw pafio::FetchError(
        "registry v2 source artifact size mismatch for " + entry.package + "@" + entry.version
      );
    }
    if (pafio::Sha256File(blob_staging) != entry.sha256) {
      throw pafio::FetchError(
        "registry v2 source artifact sha256 mismatch for " + entry.package + "@" + entry.version
      );
    }

    ValidateTarListingPaths(blob_staging);
    fs::create_directories(checkout_staging);
    const pafio::ProcessResult extract = pafio::RunProcess<pafio::CacheError>({
      .program = pafio::ResolvedTarPath(),
      .args = {
        "--no-same-owner",
        "--no-same-permissions",
        "-xf",
        pafio::ProcessPathString(blob_staging),
        "-C",
        pafio::ProcessPathString(checkout_staging),
      },
      .search_path = false,
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry process",
    });
    if (extract.exit_code != 0) {
      throw pafio::CacheError(
        "failed to extract registry artifact '" + blob_staging.string() +
        "': " + pafio::DescribeProcessFailure(extract)
      );
    }
    const fs::path staged_snapshot_root = DetectSnapshotRoot(checkout_staging);
    const fs::path snapshot_relative =
      staged_snapshot_root.lexically_relative(checkout_staging);
    pafio::AtomicWriteFile(
      checkout_staging / ".pafio-snapshot-ready",
      ReadyMarkerBytes(entry.sha256, entry.size_bytes)
    );

    std::error_code rename_error;
    fs::rename(blob_staging, blob_path, rename_error);
    if (rename_error) {
      throw pafio::CacheError(
        "failed to finalize verified registry artifact: " + blob_path.string()
      );
    }
    fs::rename(checkout_staging, checkout_root, rename_error);
    if (rename_error) {
      throw pafio::CacheError(
        "failed to finalize verified registry checkout: " + checkout_root.string()
      );
    }
    return snapshot_relative.empty()
      ? checkout_root
      : checkout_root / snapshot_relative;
  }
  catch (...) {
    std::error_code ignored;
    fs::remove(blob_staging, ignored);
    fs::remove_all(checkout_staging, ignored);
    throw;
  }
}

}  // namespace

namespace pafio
{

RegistryMaterializationResult
MaterializeRegistryPackage(
  const std::string &registry_root,
  const std::string &package_name,
  const std::string &version,
  const bool offline,
  const std::optional<std::string> &locked_sha256
) {
  const RegistryReadSecurityDecision security = ResolveRegistryReadSecurity({
    .registry_root = registry_root,
    .offline = offline,
  });
  const fs::path pafio_home = ResolvePafioHome();
  ValidateRegistryPackageIdentity(package_name);
  const auto [package_namespace, _short_name] = SplitPackageName(package_name);

  std::optional<std::string> trusted_root_sha256;
  int timestamp_floor = 0;
  int snapshot_floor = 0;
  if (IsHttpRegistry(security.registry_root)) {
    const std::optional<RegistryTrustPin> trust_pin = ResolveRegistryTrustPin(pafio_home, security.registry_root);
    if (!trust_pin.has_value()) {
      throw FetchError(
        "remote registry is not trusted: import a platform registry descriptor with 'pafio registry trust import' before fetching " + security.registry_root
      );
    }
    trusted_root_sha256 = trust_pin->root_sha256;
    timestamp_floor = trust_pin->timestamp_floor;
    snapshot_floor = trust_pin->snapshot_floor;
  }

  if (locked_sha256.has_value()) {
    if (!IsRegistrySha256Digest(*locked_sha256)) {
      throw FetchError(
        "locked registry package digest must be a lowercase sha256 hex digest"
      );
    }
    const FileLockGuard digest_lock = AcquireFileLock(
      DigestLockRoot(pafio_home, *locked_sha256),
      FileLockScope::kCache
    );
    std::string cache_failure;
    if (const std::optional<VerifiedCacheHit> cached = InspectVerifiedCache(
          pafio_home,
          package_name,
          version,
          *locked_sha256,
          std::nullopt,
          cache_failure);
        cached.has_value()) {
      return {
        .registry_root = security.registry_root,
        .package_name = package_name,
        .version = version,
        .sha256 = *locked_sha256,
        .snapshot_root = cached->snapshot_root,
      };
    }
    if (offline) {
      throw CacheError(
        "offline registry cache integrity failure for " + package_name + "@" +
        version + ": " + cache_failure
      );
    }
  }

  std::map<std::string, std::string> tuf_objects;
  const RegistryConfig config = LoadConfig(pafio_home, security.registry_root, security.request_headers, offline, tuf_objects);
  const std::string targets_relative = config.targets_prefix + package_namespace + ".json";

  const auto load_object = [&](const std::string &relative_path, const std::string &context)
  {
    tuf_objects[NormalizeRelativeRegistryPath(relative_path, context).generic_string()] = LoadRegistryObjectText(
      pafio_home,
      security.registry_root,
      security.request_headers,
      relative_path,
      context,
      offline,
      tuf_objects
    );
  };
  load_object("config.json", "registry v2 config");
  load_object("trust/root.json", "registry v2 root metadata");
  load_object("trust/timestamp.json", "registry v2 timestamp metadata");
  load_object("trust/snapshot.json", "registry v2 snapshot metadata");
  load_object("log/checkpoint.json", "registry v2 transparency checkpoint");
  load_object(targets_relative, "registry v2 targets metadata");

  // Snapshot may reference additional hashed objects; load any meta paths needed for the package targets hash check.
  {
    const json snapshot_envelope = ParseJsonObject(tuf_objects["trust/snapshot.json"], "registry v2 snapshot metadata");
    if (snapshot_envelope.contains("signed") && snapshot_envelope["signed"].is_object() && snapshot_envelope["signed"].contains("meta") && snapshot_envelope["signed"]["meta"].is_object()) {
      for (auto it = snapshot_envelope["signed"]["meta"].begin(); it != snapshot_envelope["signed"]["meta"].end(); ++it) {
        const std::string relative = NormalizeRelativeRegistryPath(it.key(), "registry v2 snapshot meta path").generic_string();
        if (tuf_objects.find(relative) == tuf_objects.end()) {
          // Only fetch the package namespace targets eagerly; other namespaces are not required for package materialization.
          if (relative == NormalizeRelativeRegistryPath(targets_relative, "registry v2 targets path").generic_string()) {
            load_object(relative, "registry v2 targets metadata");
          }
        }
      }
    }
  }

  const TufVerifyResult tuf = VerifyTufChainForPackage(
    tuf_objects,
    package_name,
    package_namespace,
    trusted_root_sha256,
    targets_relative,
    timestamp_floor,
    snapshot_floor
  );
  if (!tuf.ok) {
    throw FetchError(
      std::string("registry v2 TUF verification failed") + (tuf.error_code.empty() ? "" : " (" + tuf.error_code + ")") + ": " + tuf.error
    );
  }
  if (trusted_root_sha256.has_value()) {
    // Persist the accepted metadata versions as the new replay floors before
    // any later stage can fail: the metadata itself was authenticated, so the
    // floor is safe to keep even if package materialization aborts.
    RaiseRegistryTrustFloors(
      pafio_home,
      security.registry_root,
      tuf.timestamp_version,
      tuf.snapshot_version
    );
  }

  const RegistryPackageMetadata metadata =
    LoadPackageMetadata(pafio_home, security.registry_root, security.request_headers, config, package_name, version, offline, tuf_objects);
  const RegistryEntry entry =
    LoadEntry(pafio_home, security.registry_root, security.request_headers, package_name, version, metadata, offline, tuf_objects);
  if (locked_sha256.has_value() && entry.sha256 != *locked_sha256) {
    throw FetchError(
      "registry content digest changed for locked package " + package_name + "@" +
      version + ": expected " + *locked_sha256 + " but registry declared " + entry.sha256
    );
  }

  const FileLockGuard digest_lock = AcquireFileLock(
    DigestLockRoot(pafio_home, entry.sha256),
    FileLockScope::kCache
  );
  const fs::path snapshot_root = CommitRegistryEntry(
    pafio_home,
    security.registry_root,
    security.request_headers,
    entry,
    offline
  );

  return {
    .registry_root = security.registry_root,
    .package_name = entry.package,
    .version = entry.version,
    .sha256 = entry.sha256,
    .snapshot_root = snapshot_root,
  };
}

}  // namespace pafio
