#include "PafioRegistryClient/Client.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"
#include "PafioSecurity/RegistryTrust.hpp"

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
  pafio::ValidateRegistryPackageIdentity(package_name);
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
    throw pafio::FetchError("failed to open " + context + ": " + path.string());
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
      throw pafio::CacheError("failed to open temporary " + context + " for write: " + temp_path.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good())
    {
      throw pafio::CacheError("failed to write temporary " + context + ": " + temp_path.string());
    }
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw pafio::CacheError("failed to finalize " + context + ": " + path.string());
  }
}

json ParseJsonObject(const std::string &text, const std::string &context)
{
  try
  {
    json payload = json::parse(text);
    if (!payload.is_object())
    {
      throw pafio::FetchError(context + " must be a JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &)
  {
    throw pafio::FetchError(context + " is not valid JSON");
  }
}

fs::path NormalizeRelativeRegistryPath(const std::string &relative_path, const std::string &context)
{
  return pafio::NormalizeRegistryObjectPath(relative_path, context);
}

struct RegistryConfig
{
  std::string targets_prefix = "trust/targets/";
};

RegistryConfig ValidateConfig(const json &config, const std::string &context)
{
  if (config.value("protocol", "") != "pafio-static-registry" || config.value("protocol_version", 0) != 2)
  {
    throw pafio::FetchError(context + " does not match the supported registry v2 protocol");
  }

  RegistryConfig parsed;
  if (config.contains("paths"))
  {
    if (!config["paths"].is_object())
    {
      throw pafio::FetchError(context + " paths must be a JSON object");
    }
    parsed.targets_prefix = config["paths"].value("targets_prefix", "trust/targets/");
  }
  if (parsed.targets_prefix.empty())
  {
    throw pafio::FetchError(context + " is missing paths.targets_prefix");
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
    throw pafio::FetchError(context + " must contain a signed object");
  }
  const json &signed_payload = payload["signed"];
  if (signed_payload.value("type", "") != "targets")
  {
    throw pafio::FetchError(context + " signed.type must equal 'targets'");
  }
  if (signed_payload.value("namespace", "") != package_namespace)
  {
    throw pafio::FetchError(
        context + " namespace mismatch: expected '" + package_namespace + "' but found '" +
        signed_payload.value("namespace", "") + "'");
  }
  if (!signed_payload.contains("packages") || !signed_payload["packages"].is_object())
  {
    throw pafio::FetchError(context + " signed.packages must be a JSON object");
  }
  const auto package_it = signed_payload["packages"].find(package_name);
  if (package_it == signed_payload["packages"].end() || !package_it->is_object())
  {
    throw pafio::FetchError("registry v2 targets metadata is missing package: " + package_name);
  }

  const std::string index_path = package_it->value("index_path", "");
  if (index_path.empty())
  {
    throw pafio::FetchError("registry v2 targets metadata is missing index_path for " + package_name);
  }
  if (!package_it->contains("releases") || !(*package_it)["releases"].is_object())
  {
    throw pafio::FetchError("registry v2 targets metadata is missing releases for " + package_name);
  }
  if ((*package_it)["releases"].find(version) == (*package_it)["releases"].end())
  {
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

RegistryEntry ValidateEntry(const json &entry, const std::string &expected_package, const std::string &expected_version)
{
  const std::string package = entry.value("package", "");
  const std::string version = entry.value("version", "");
  if (package != expected_package)
  {
    throw pafio::FetchError("registry v2 entry package mismatch: expected '" + expected_package + "' but found '" + package + "'");
  }
  if (version != expected_version)
  {
    throw pafio::FetchError("registry v2 entry version mismatch: expected '" + expected_version + "' but found '" + version + "'");
  }
  if (!entry.contains("source_artifact") || !entry["source_artifact"].is_object())
  {
    throw pafio::FetchError("registry v2 entry is missing source_artifact");
  }
  const json &artifact = entry["source_artifact"];
  const std::string sha256 = artifact.value("sha256", "");
  const std::string artifact_path = artifact.value("path", "");
  if (!pafio::IsRegistrySha256Digest(sha256))
  {
    throw pafio::FetchError("registry v2 source_artifact is missing a valid sha256 digest");
  }
  if (!artifact.contains("size_bytes") || !artifact["size_bytes"].is_number_unsigned())
  {
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
    throw pafio::FetchError("failed to read registry response headers: " + headers_path.string());
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
  const pafio::ProcessResult result = pafio::RunProcess<pafio::CacheError>({
      .program = "curl",
      .args = args,
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (result.exit_code != 0)
  {
    std::error_code ignored;
    fs::remove(temp_path, ignored);
    fs::remove(headers_path, ignored);
    throw pafio::FetchError(
        "failed to fetch registry object '" + url + "': " +
        pafio::DescribeProcessFailure(result));
  }
  const std::string media_type = ReadLastContentType(headers_path);
  std::error_code ignored;
  fs::remove(headers_path, ignored);
  if (!IsAllowedRegistryMediaType(media_type, object_kind))
  {
    fs::remove(temp_path, ignored);
    throw pafio::FetchError("registry object has unsupported media type '" + (media_type.empty() ? "<missing>" : media_type) + "': " + url);
  }
  std::error_code size_ec;
  const uintmax_t downloaded_size = fs::file_size(temp_path, size_ec);
  if (size_ec || downloaded_size > max_bytes)
  {
    fs::remove(temp_path);
    throw pafio::FetchError("registry object exceeded response limit '" + url + "'");
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw pafio::CacheError("failed to finalize registry artifact cache: " + path.string());
  }
}

std::string JoinUrl(const std::string &root, const std::string &relative_path)
{
  return NormalizeRegistryRoot(root) + "/" + NormalizeRelativeRegistryPath(relative_path, "registry object path").generic_string();
}

fs::path RegistryObjectCachePath(const fs::path &pafio_home, const std::string &registry_root, const std::string &relative_path)
{
  return pafio::RegistryIndexCacheRoot(pafio_home) / Hex64(Fnv1a64(registry_root)) /
         NormalizeRelativeRegistryPath(relative_path, "registry cache object path");
}

fs::path BlobCachePath(const fs::path &pafio_home, const std::string &sha256)
{
  return pafio::RegistryBlobCacheRoot(pafio_home) / sha256.substr(0U, 2U) / sha256.substr(2U, 2U) / (sha256 + ".tar");
}

fs::path CheckoutPath(const fs::path &pafio_home, const std::string &package_name, const std::string &version, const std::string &sha256)
{
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return pafio::RegistryCheckoutRoot(pafio_home) / package_namespace / short_name / version / sha256;
}

std::string LoadRegistryObjectText(
    const fs::path &pafio_home,
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
    throw pafio::FetchError("registry root must use file://, http://, or https://: " + registry_root);
  }

  const fs::path cache_path = RegistryObjectCachePath(pafio_home, registry_root, normalized_relative.generic_string());
  if (offline)
  {
    if (!fs::exists(cache_path))
    {
      throw pafio::FetchError("offline mode is missing cached " + context + " for " + registry_root);
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
    const fs::path &pafio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const bool offline)
{
  return ValidateConfig(
      ParseJsonObject(
          LoadRegistryObjectText(pafio_home, registry_root, request_headers, "config.json", "registry v2 config", offline),
          "registry v2 config"),
      "registry v2 config");
}

RegistryPackageMetadata LoadPackageMetadata(
    const fs::path &pafio_home,
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
              pafio_home,
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
    const fs::path &pafio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const std::string &package_name,
    const std::string &version,
    const RegistryPackageMetadata &metadata,
    const bool offline)
{
  const std::string text = LoadRegistryObjectText(
      pafio_home,
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

  throw pafio::FetchError("registry v2 index does not contain package version: " + package_name + "@" + version);
}

void MaterializeArtifact(
    const fs::path &pafio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const RegistryEntry &entry,
    const bool offline)
{
  const fs::path blob_cache_path = BlobCachePath(pafio_home, entry.sha256);
  if (fs::exists(blob_cache_path))
  {
    const std::string actual_sha256 = pafio::Sha256File(blob_cache_path);
    if (actual_sha256 != entry.sha256)
    {
      throw pafio::CacheError("cached registry artifact sha256 mismatch: " + blob_cache_path.string());
    }
    std::error_code size_ec;
    const uintmax_t cached_size = fs::file_size(blob_cache_path, size_ec);
    if (size_ec || cached_size != entry.size_bytes)
    {
      throw pafio::CacheError("cached registry artifact size mismatch: " + blob_cache_path.string());
    }
    return;
  }

  const fs::path artifact_relative = NormalizeRelativeRegistryPath(entry.artifact_path, "registry v2 source artifact path");
  if (IsFileRegistry(registry_root))
  {
    const fs::path source_blob = FileUrlToPath(registry_root) / artifact_relative;
    if (!fs::exists(source_blob))
    {
      throw pafio::FetchError("registry v2 source artifact not found: " + source_blob.string());
    }
    fs::create_directories(blob_cache_path.parent_path());
    std::error_code ec;
    fs::copy_file(source_blob, blob_cache_path, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      throw pafio::CacheError("failed to cache registry v2 source artifact: " + blob_cache_path.string());
    }
  }
  else
  {
    if (offline)
    {
      throw pafio::FetchError("offline mode is missing cached registry artifact for " + entry.package + "@" + entry.version);
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
    throw pafio::FetchError("registry v2 source artifact size mismatch for " + entry.package + "@" + entry.version);
  }
  const std::string actual_sha256 = pafio::Sha256File(blob_cache_path);
  if (actual_sha256 != entry.sha256)
  {
    throw pafio::FetchError("registry v2 source artifact sha256 mismatch for " + entry.package + "@" + entry.version);
  }
}

fs::path DetectSnapshotRoot(const fs::path &checkout_root)
{
  if (fs::exists(checkout_root / "pafio.toml"))
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

  if (subdirectories.size() == 1U && fs::exists(subdirectories.front() / "pafio.toml"))
  {
    return subdirectories.front();
  }

  throw pafio::CacheError("registry package snapshot does not contain pafio.toml: " + checkout_root.string());
}

void ValidateTarListingPaths(const fs::path &blob_cache_path)
{
  const pafio::ProcessResult path_listing = pafio::RunProcess<pafio::CacheError>({
      .program = "tar",
      .args = {"-tf", blob_cache_path.string()},
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (path_listing.exit_code != 0)
  {
    throw pafio::CacheError(
        "failed to list registry artifact '" + blob_cache_path.string() + "': " +
        pafio::DescribeProcessFailure(path_listing));
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
      throw pafio::CacheError("registry archive member path is empty after normalization: " + original_entry_path);
    }
    NormalizeRelativeRegistryPath(entry_path, "registry archive member path");
    if (entry_path == "pafio.toml" || entry_path.ends_with("/pafio.toml"))
    {
      ++manifest_candidates;
    }
  }
  if (manifest_candidates != 1U)
  {
    throw pafio::CacheError("registry archive must contain exactly one pafio.toml manifest");
  }

  const pafio::ProcessResult verbose_listing = pafio::RunProcess<pafio::CacheError>({
      .program = "tar",
      .args = {"-tvf", blob_cache_path.string()},
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (verbose_listing.exit_code != 0)
  {
    throw pafio::CacheError(
        "failed to inspect registry artifact '" + blob_cache_path.string() + "': " +
        pafio::DescribeProcessFailure(verbose_listing));
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
      throw pafio::CacheError("registry archive member type is not allowed: " + verbose_line);
    }
  }
}

fs::path EnsureCheckout(const fs::path &pafio_home, const RegistryEntry &entry)
{
  const fs::path checkout_root = CheckoutPath(pafio_home, entry.package, entry.version, entry.sha256);
  const fs::path ready_marker = checkout_root / ".pafio-snapshot-ready";
  if (fs::exists(ready_marker))
  {
    return DetectSnapshotRoot(checkout_root);
  }

  std::error_code ignored;
  fs::remove_all(checkout_root, ignored);
  fs::create_directories(checkout_root);

  const fs::path blob_cache_path = BlobCachePath(pafio_home, entry.sha256);
  ValidateTarListingPaths(blob_cache_path);
  const pafio::ProcessResult extract = pafio::RunProcess<pafio::CacheError>({
      .program = "tar",
      .args = {"--no-same-owner", "--no-same-permissions", "-xf", blob_cache_path.string(), "-C", checkout_root.string()},
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry process",
  });
  if (extract.exit_code != 0)
  {
    throw pafio::CacheError(
        "failed to extract registry artifact '" + blob_cache_path.string() + "': " +
        pafio::DescribeProcessFailure(extract));
  }
  const fs::path snapshot_root = DetectSnapshotRoot(checkout_root);

  std::ofstream marker(ready_marker);
  if (!marker)
  {
    throw pafio::CacheError("failed to write registry ready marker: " + ready_marker.string());
  }
  marker << "ready\n";
  return snapshot_root;
}

}  // namespace

namespace pafio
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
  const fs::path pafio_home = ResolvePafioHome();

  if (IsHttpRegistry(security.registry_root))
  {
    const std::optional<RegistryTrustPin> trust_pin = ResolveRegistryTrustPin(pafio_home, security.registry_root);
    if (!trust_pin.has_value())
    {
      throw FetchError(
          "remote registry is not trusted: import a platform registry descriptor with 'pafio registry trust import' before fetching " +
          security.registry_root);
    }
    const std::string root_metadata = LoadRegistryObjectText(
        pafio_home,
        security.registry_root,
        security.request_headers,
        "trust/root.json",
        "registry v2 root metadata",
        offline);
    const std::string actual_root_sha256 = Sha256Text(root_metadata);
    if (actual_root_sha256 != trust_pin->root_sha256)
    {
      throw FetchError(
          "registry v2 root metadata does not match the imported platform descriptor pin for " +
          security.registry_root);
    }
  }

  const RegistryConfig config = LoadConfig(pafio_home, security.registry_root, security.request_headers, offline);
  const RegistryPackageMetadata metadata =
      LoadPackageMetadata(pafio_home, security.registry_root, security.request_headers, config, package_name, version, offline);
  const RegistryEntry entry =
      LoadEntry(pafio_home, security.registry_root, security.request_headers, package_name, version, metadata, offline);
  MaterializeArtifact(pafio_home, security.registry_root, security.request_headers, entry, offline);
  const fs::path snapshot_root = EnsureCheckout(pafio_home, entry);

  return {
      .registry_root = security.registry_root,
      .package_name = entry.package,
      .version = entry.version,
      .sha256 = entry.sha256,
      .snapshot_root = snapshot_root,
  };
}

}  // namespace pafio
