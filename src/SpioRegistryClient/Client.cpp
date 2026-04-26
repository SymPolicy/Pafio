#include "SpioRegistryClient/Client.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Process.hpp"
#include "SpioCore/Sha256.hpp"
#include "SpioSecurity/RegistrySecurity.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
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

std::string NormalizeRegistryRoot(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

bool IsHttpRegistry(const std::string &registry_root)
{
  return registry_root.starts_with("http://") || registry_root.starts_with("https://");
}

bool IsFileRegistry(const std::string &registry_root)
{
  return registry_root.starts_with("file://");
}

fs::path FileUrlToPath(const std::string &registry_root)
{
  return fs::path(registry_root.substr(std::string("file://").size()));
}

std::pair<std::string, std::string> SplitPackageName(const std::string &package_name)
{
  spio::ValidateRegistryPackageIdentity(package_name);
  const size_t slash = package_name.find('/');
  return {
      package_name.substr(0U, slash),
      package_name.substr(slash + 1U),
  };
}

uint64_t Fnv1a64(const std::string &value)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : value)
  {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string Hex64(const uint64_t value)
{
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

std::string ReadTextFile(const fs::path &path, const std::string &context)
{
  std::ifstream in(path);
  if (!in)
  {
    throw spio::FetchError("failed to open " + context + ": " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void WriteFileAtomically(const fs::path &path, std::string_view content, const std::string &context)
{
  fs::create_directories(path.parent_path());
  const fs::path temp_path = path.parent_path() / (path.filename().string() + ".tmp");
  {
    std::ofstream out(temp_path, std::ios::binary);
    if (!out)
    {
      throw spio::CacheError("failed to open temporary " + context + " for write: " + temp_path.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good())
    {
      throw spio::CacheError("failed to write temporary " + context + ": " + temp_path.string());
    }
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::CacheError("failed to finalize " + context + ": " + path.string());
  }
}

json ParseJsonObject(const std::string &text, const std::string &context)
{
  try
  {
    json payload = json::parse(text);
    if (!payload.is_object())
    {
      throw spio::FetchError(context + " must be a JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &)
  {
    throw spio::FetchError(context + " is not valid JSON");
  }
}

fs::path NormalizeRelativeRegistryPath(const std::string &relative_path, const std::string &context)
{
  return spio::NormalizeRegistryObjectPath(relative_path, context);
}

struct RegistryConfig
{
  std::string targets_prefix = "trust/targets/";
};

RegistryConfig ValidateConfig(const json &config, const std::string &context)
{
  if (config.value("protocol", "") != "spio-static-registry" || config.value("protocol_version", 0) != 2)
  {
    throw spio::FetchError(context + " does not match the supported registry v2 protocol");
  }

  RegistryConfig parsed;
  if (config.contains("paths"))
  {
    if (!config["paths"].is_object())
    {
      throw spio::FetchError(context + " paths must be a JSON object");
    }
    parsed.targets_prefix = config["paths"].value("targets_prefix", "trust/targets/");
  }
  if (parsed.targets_prefix.empty())
  {
    throw spio::FetchError(context + " is missing paths.targets_prefix");
  }
  return parsed;
}

struct RegistryPackageMetadata
{
  std::string index_path;
};

RegistryPackageMetadata ValidateTargetsMetadata(
    const json &payload,
    const std::string &package_name,
    const std::string &package_namespace,
    const std::string &version,
    const std::string &context)
{
  if (!payload.contains("signed") || !payload["signed"].is_object())
  {
    throw spio::FetchError(context + " must contain a signed object");
  }
  const json &signed_payload = payload["signed"];
  if (signed_payload.value("type", "") != "targets")
  {
    throw spio::FetchError(context + " signed.type must equal 'targets'");
  }
  if (signed_payload.value("namespace", "") != package_namespace)
  {
    throw spio::FetchError(
        context + " namespace mismatch: expected '" + package_namespace + "' but found '" +
        signed_payload.value("namespace", "") + "'");
  }
  if (!signed_payload.contains("packages") || !signed_payload["packages"].is_object())
  {
    throw spio::FetchError(context + " signed.packages must be a JSON object");
  }
  const auto package_it = signed_payload["packages"].find(package_name);
  if (package_it == signed_payload["packages"].end() || !package_it->is_object())
  {
    throw spio::FetchError("registry v2 targets metadata is missing package: " + package_name);
  }

  const std::string index_path = package_it->value("index_path", "");
  if (index_path.empty())
  {
    throw spio::FetchError("registry v2 targets metadata is missing index_path for " + package_name);
  }
  if (!package_it->contains("releases") || !(*package_it)["releases"].is_object())
  {
    throw spio::FetchError("registry v2 targets metadata is missing releases for " + package_name);
  }
  if ((*package_it)["releases"].find(version) == (*package_it)["releases"].end())
  {
    throw spio::FetchError("registry v2 targets metadata does not contain version " + package_name + "@" + version);
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

RegistryEntry ValidateEntry(const json &entry, const std::string &expected_package, const std::string &expected_version)
{
  const std::string package = entry.value("package", "");
  const std::string version = entry.value("version", "");
  if (package != expected_package)
  {
    throw spio::FetchError("registry v2 entry package mismatch: expected '" + expected_package + "' but found '" + package + "'");
  }
  if (version != expected_version)
  {
    throw spio::FetchError("registry v2 entry version mismatch: expected '" + expected_version + "' but found '" + version + "'");
  }
  if (!entry.contains("source_artifact") || !entry["source_artifact"].is_object())
  {
    throw spio::FetchError("registry v2 entry is missing source_artifact");
  }
  const json &artifact = entry["source_artifact"];
  const std::string sha256 = artifact.value("sha256", "");
  const std::string artifact_path = artifact.value("path", "");
  if (!spio::IsRegistrySha256Digest(sha256))
  {
    throw spio::FetchError("registry v2 source_artifact is missing a valid sha256 digest");
  }
  if (!artifact.contains("size_bytes") || !artifact["size_bytes"].is_number_unsigned())
  {
    throw spio::FetchError("registry v2 source_artifact is missing a valid size_bytes value");
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

std::string TrimAscii(std::string value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
  {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
  {
    value.pop_back();
  }
  return value;
}

std::string LowerAscii(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string ReadLastContentType(const fs::path &headers_path)
{
  std::ifstream in(headers_path);
  if (!in)
  {
    throw spio::FetchError("failed to read registry response headers: " + headers_path.string());
  }
  std::string line;
  std::string media_type;
  while (std::getline(in, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    const std::string lowered = LowerAscii(line);
    constexpr std::string_view prefix = "content-type:";
    if (lowered.starts_with(prefix))
    {
      media_type = TrimAscii(line.substr(prefix.size()));
      const size_t semicolon = media_type.find(';');
      if (semicolon != std::string::npos)
      {
        media_type = media_type.substr(0U, semicolon);
      }
      media_type = LowerAscii(TrimAscii(media_type));
    }
  }
  return media_type;
}

bool IsAllowedRegistryMediaType(const std::string &media_type, const RegistryRemoteObjectKind kind)
{
  if (kind == RegistryRemoteObjectKind::kArtifact)
  {
    return media_type == "application/octet-stream" || media_type == "application/x-tar" ||
           media_type == "application/tar";
  }
  return media_type == "application/json" || media_type == "application/octet-stream" ||
         media_type == "application/x-ndjson" || media_type == "application/jsonl" ||
         media_type == "text/plain";
}

std::vector<std::string> CurlReadPolicyArgs(const size_t max_bytes)
{
  return {
      "--connect-timeout",
      "10",
      "--max-time",
      "30",
      "--speed-time",
      "10",
      "--speed-limit",
      "1024",
      "--max-filesize",
      std::to_string(max_bytes),
  };
}

void FetchUrlToFile(
    const std::string &url,
    const fs::path &path,
    const std::vector<std::string> &request_headers,
    const uintmax_t max_bytes,
    const RegistryRemoteObjectKind object_kind)
{
  fs::create_directories(path.parent_path());
  const fs::path temp_path = path.parent_path() / (path.filename().string() + ".tmp");
  const fs::path headers_path = path.parent_path() / (path.filename().string() + ".headers.tmp");
  std::vector<std::string> args{"-fsSL", "-D", headers_path.string()};
  const std::vector<std::string> policy_args = CurlReadPolicyArgs(static_cast<size_t>(max_bytes));
  args.insert(args.end(), policy_args.begin(), policy_args.end());
  for (const std::string &header : request_headers)
  {
    args.push_back("-H");
    args.push_back(header);
  }
  args.push_back("-o");
  args.push_back(temp_path.string());
  args.push_back(url);
  const spio::ProcessResult result = spio::RunProcess<spio::CacheError>({
      .program = "curl",
      .args = args,
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (result.exit_code != 0)
  {
    std::error_code ignored;
    fs::remove(temp_path, ignored);
    fs::remove(headers_path, ignored);
    throw spio::FetchError(
        "failed to fetch registry object '" + url + "': " +
        spio::DescribeProcessFailure(result));
  }
  const std::string media_type = ReadLastContentType(headers_path);
  std::error_code ignored;
  fs::remove(headers_path, ignored);
  if (!IsAllowedRegistryMediaType(media_type, object_kind))
  {
    fs::remove(temp_path, ignored);
    throw spio::FetchError("registry object has unsupported media type '" + (media_type.empty() ? "<missing>" : media_type) + "': " + url);
  }
  std::error_code size_ec;
  const uintmax_t downloaded_size = fs::file_size(temp_path, size_ec);
  if (size_ec || downloaded_size > max_bytes)
  {
    fs::remove(temp_path);
    throw spio::FetchError("registry object exceeded response limit '" + url + "'");
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::CacheError("failed to finalize registry artifact cache: " + path.string());
  }
}

std::string JoinUrl(const std::string &root, const std::string &relative_path)
{
  return NormalizeRegistryRoot(root) + "/" + NormalizeRelativeRegistryPath(relative_path, "registry object path").generic_string();
}

fs::path RegistryObjectCachePath(const fs::path &spio_home, const std::string &registry_root, const std::string &relative_path)
{
  return spio::RegistryIndexCacheRoot(spio_home) / Hex64(Fnv1a64(registry_root)) /
         NormalizeRelativeRegistryPath(relative_path, "registry cache object path");
}

fs::path BlobCachePath(const fs::path &spio_home, const std::string &sha256)
{
  return spio::RegistryBlobCacheRoot(spio_home) / sha256.substr(0U, 2U) / sha256.substr(2U, 2U) / (sha256 + ".tar");
}

fs::path CheckoutPath(const fs::path &spio_home, const std::string &package_name, const std::string &version, const std::string &sha256)
{
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return spio::RegistryCheckoutRoot(spio_home) / package_namespace / short_name / version / sha256;
}

std::string LoadRegistryObjectText(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const std::string &relative_path,
    const std::string &context,
    const bool offline)
{
  const fs::path normalized_relative = NormalizeRelativeRegistryPath(relative_path, context);
  if (IsFileRegistry(registry_root))
  {
    return ReadTextFile(FileUrlToPath(registry_root) / normalized_relative, context);
  }
  if (!IsHttpRegistry(registry_root))
  {
    throw spio::FetchError("registry root must use file://, http://, or https://: " + registry_root);
  }

  const fs::path cache_path = RegistryObjectCachePath(spio_home, registry_root, normalized_relative.generic_string());
  if (offline)
  {
    if (!fs::exists(cache_path))
    {
      throw spio::FetchError("offline mode is missing cached " + context + " for " + registry_root);
    }
    return ReadTextFile(cache_path, "cached " + context);
  }

  FetchUrlToFile(
      JoinUrl(registry_root, normalized_relative.generic_string()),
      cache_path,
      request_headers,
      kRegistryMetadataMaxBytes,
      RegistryRemoteObjectKind::kMetadata);
  return ReadTextFile(cache_path, "cached " + context);
}

RegistryConfig LoadConfig(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const bool offline)
{
  return ValidateConfig(
      ParseJsonObject(
          LoadRegistryObjectText(spio_home, registry_root, request_headers, "config.json", "registry v2 config", offline),
          "registry v2 config"),
      "registry v2 config");
}

RegistryPackageMetadata LoadPackageMetadata(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const RegistryConfig &config,
    const std::string &package_name,
    const std::string &version,
    const bool offline)
{
  const auto [package_namespace, _short_name] = SplitPackageName(package_name);
  return ValidateTargetsMetadata(
      ParseJsonObject(
          LoadRegistryObjectText(
              spio_home,
              registry_root,
              request_headers,
              config.targets_prefix + package_namespace + ".json",
              "registry v2 targets metadata",
              offline),
          "registry v2 targets metadata"),
      package_name,
      package_namespace,
      version,
      "registry v2 targets metadata");
}

RegistryEntry LoadEntry(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const std::string &package_name,
    const std::string &version,
    const RegistryPackageMetadata &metadata,
    const bool offline)
{
  const std::string text = LoadRegistryObjectText(
      spio_home,
      registry_root,
      request_headers,
      metadata.index_path,
      "registry v2 index",
      offline);

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line))
  {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos)
    {
      continue;
    }
    const json entry = ParseJsonObject(line, "registry v2 index record");
    if (entry.value("package", "") == package_name && entry.value("version", "") == version)
    {
      return ValidateEntry(entry, package_name, version);
    }
  }

  throw spio::FetchError("registry v2 index does not contain package version: " + package_name + "@" + version);
}

void MaterializeArtifact(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const RegistryEntry &entry,
    const bool offline)
{
  const fs::path blob_cache_path = BlobCachePath(spio_home, entry.sha256);
  if (fs::exists(blob_cache_path))
  {
    const std::string actual_sha256 = spio::Sha256File(blob_cache_path);
    if (actual_sha256 != entry.sha256)
    {
      throw spio::CacheError("cached registry artifact sha256 mismatch: " + blob_cache_path.string());
    }
    std::error_code size_ec;
    const uintmax_t cached_size = fs::file_size(blob_cache_path, size_ec);
    if (size_ec || cached_size != entry.size_bytes)
    {
      throw spio::CacheError("cached registry artifact size mismatch: " + blob_cache_path.string());
    }
    return;
  }

  const fs::path artifact_relative = NormalizeRelativeRegistryPath(entry.artifact_path, "registry v2 source artifact path");
  if (IsFileRegistry(registry_root))
  {
    const fs::path source_blob = FileUrlToPath(registry_root) / artifact_relative;
    if (!fs::exists(source_blob))
    {
      throw spio::FetchError("registry v2 source artifact not found: " + source_blob.string());
    }
    fs::create_directories(blob_cache_path.parent_path());
    std::error_code ec;
    fs::copy_file(source_blob, blob_cache_path, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      throw spio::CacheError("failed to cache registry v2 source artifact: " + blob_cache_path.string());
    }
  }
  else
  {
    if (offline)
    {
      throw spio::FetchError("offline mode is missing cached registry artifact for " + entry.package + "@" + entry.version);
    }
    FetchUrlToFile(
        JoinUrl(registry_root, artifact_relative.generic_string()),
        blob_cache_path,
        request_headers,
        kRegistryArtifactMaxBytes,
        RegistryRemoteObjectKind::kArtifact);
  }

  std::error_code size_ec;
  const uintmax_t actual_size = fs::file_size(blob_cache_path, size_ec);
  if (size_ec || actual_size != entry.size_bytes)
  {
    throw spio::FetchError("registry v2 source artifact size mismatch for " + entry.package + "@" + entry.version);
  }
  const std::string actual_sha256 = spio::Sha256File(blob_cache_path);
  if (actual_sha256 != entry.sha256)
  {
    throw spio::FetchError("registry v2 source artifact sha256 mismatch for " + entry.package + "@" + entry.version);
  }
}

fs::path DetectSnapshotRoot(const fs::path &checkout_root)
{
  if (fs::exists(checkout_root / "spio.toml"))
  {
    return checkout_root;
  }

  std::vector<fs::path> subdirectories;
  for (const fs::directory_entry &entry : fs::directory_iterator(checkout_root))
  {
    if (entry.is_directory())
    {
      subdirectories.push_back(entry.path());
    }
  }

  if (subdirectories.size() == 1U && fs::exists(subdirectories.front() / "spio.toml"))
  {
    return subdirectories.front();
  }

  throw spio::CacheError("registry package snapshot does not contain spio.toml: " + checkout_root.string());
}

void ValidateTarListingPaths(const fs::path &blob_cache_path)
{
  const spio::ProcessResult path_listing = spio::RunProcess<spio::CacheError>({
      .program = "tar",
      .args = {"-tf", blob_cache_path.string()},
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (path_listing.exit_code != 0)
  {
    throw spio::CacheError(
        "failed to list registry artifact '" + blob_cache_path.string() + "': " +
        spio::DescribeProcessFailure(path_listing));
  }
  std::istringstream path_lines(path_listing.stdout_text);
  std::string entry_path;
  size_t manifest_candidates = 0;
  while (std::getline(path_lines, entry_path))
  {
    const std::string original_entry_path = entry_path;
    while (!entry_path.empty() && entry_path.back() == '/')
    {
      entry_path.pop_back();
    }
    if (entry_path.empty())
    {
      throw spio::CacheError("registry archive member path is empty after normalization: " + original_entry_path);
    }
    NormalizeRelativeRegistryPath(entry_path, "registry archive member path");
    if (entry_path == "spio.toml" || entry_path.ends_with("/spio.toml"))
    {
      ++manifest_candidates;
    }
  }
  if (manifest_candidates != 1U)
  {
    throw spio::CacheError("registry archive must contain exactly one spio.toml manifest");
  }

  const spio::ProcessResult verbose_listing = spio::RunProcess<spio::CacheError>({
      .program = "tar",
      .args = {"-tvf", blob_cache_path.string()},
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (verbose_listing.exit_code != 0)
  {
    throw spio::CacheError(
        "failed to inspect registry artifact '" + blob_cache_path.string() + "': " +
        spio::DescribeProcessFailure(verbose_listing));
  }
  std::istringstream verbose_lines(verbose_listing.stdout_text);
  std::string verbose_line;
  while (std::getline(verbose_lines, verbose_line))
  {
    if (verbose_line.empty())
    {
      continue;
    }
    const char type = verbose_line.front();
    if (type != '-' && type != 'd')
    {
      throw spio::CacheError("registry archive member type is not allowed: " + verbose_line);
    }
  }
}

fs::path EnsureCheckout(const fs::path &spio_home, const RegistryEntry &entry)
{
  const fs::path checkout_root = CheckoutPath(spio_home, entry.package, entry.version, entry.sha256);
  const fs::path ready_marker = checkout_root / ".spio-snapshot-ready";
  if (fs::exists(ready_marker))
  {
    return DetectSnapshotRoot(checkout_root);
  }

  std::error_code ignored;
  fs::remove_all(checkout_root, ignored);
  fs::create_directories(checkout_root);

  const fs::path blob_cache_path = BlobCachePath(spio_home, entry.sha256);
  ValidateTarListingPaths(blob_cache_path);
  const spio::ProcessResult extract = spio::RunProcess<spio::CacheError>({
      .program = "tar",
      .args = {"--no-same-owner", "--no-same-permissions", "-xf", blob_cache_path.string(), "-C", checkout_root.string()},
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (extract.exit_code != 0)
  {
    throw spio::CacheError(
        "failed to extract registry artifact '" + blob_cache_path.string() + "': " +
        spio::DescribeProcessFailure(extract));
  }
  const fs::path snapshot_root = DetectSnapshotRoot(checkout_root);

  std::ofstream marker(ready_marker);
  if (!marker)
  {
    throw spio::CacheError("failed to write registry ready marker: " + ready_marker.string());
  }
  marker << "ready\n";
  return snapshot_root;
}

}  // namespace

namespace spio
{

RegistryMaterializationResult MaterializeRegistryPackage(
    const std::string &registry_root,
    const std::string &package_name,
    const std::string &version,
    const bool offline)
{
  const RegistryReadSecurityDecision security = ResolveRegistryReadSecurity({
      .registry_root = registry_root,
      .offline = offline,
  });
  const fs::path spio_home = ResolveSpioHome();

  const RegistryConfig config = LoadConfig(spio_home, security.registry_root, security.request_headers, offline);
  const RegistryPackageMetadata metadata =
      LoadPackageMetadata(spio_home, security.registry_root, security.request_headers, config, package_name, version, offline);
  const RegistryEntry entry =
      LoadEntry(spio_home, security.registry_root, security.request_headers, package_name, version, metadata, offline);
  MaterializeArtifact(spio_home, security.registry_root, security.request_headers, entry, offline);
  const fs::path snapshot_root = EnsureCheckout(spio_home, entry);

  return {
      .registry_root = security.registry_root,
      .package_name = entry.package,
      .version = entry.version,
      .sha256 = entry.sha256,
      .snapshot_root = snapshot_root,
  };
}

}  // namespace spio
