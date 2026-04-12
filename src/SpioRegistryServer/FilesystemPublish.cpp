#include "SpioRegistryServer/Publish.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Sha256.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

std::pair<std::string, std::string> SplitPackageName(const std::string &package_name)
{
  const size_t slash = package_name.find('/');
  if (slash == std::string::npos || slash == 0U || slash + 1U >= package_name.size())
  {
    throw spio::PublishError("package name must match namespace/name for registry publication: " + package_name);
  }
  return {
      package_name.substr(0U, slash),
      package_name.substr(slash + 1U),
  };
}

void WriteJsonFileAtomically(const fs::path &path, const json &payload)
{
  fs::create_directories(path.parent_path());
  const fs::path temp_path = path.parent_path() / (path.filename().string() + ".tmp");
  {
    std::ofstream out(temp_path);
    if (!out)
    {
      throw spio::PublishError("failed to open registry metadata for write: " + temp_path.string());
    }
    out << payload.dump(2) << '\n';
    if (!out.good())
    {
      throw spio::PublishError("failed to write registry metadata: " + temp_path.string());
    }
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::PublishError("failed to finalize registry metadata: " + path.string());
  }
}

void EnsureFilesystemRegistryMarker(const fs::path &registry_root)
{
  const fs::path marker_path = registry_root / "spio-registry.json";
  if (fs::exists(marker_path))
  {
    std::ifstream in(marker_path);
    if (!in)
    {
      throw spio::PublishError("failed to read registry marker: " + marker_path.string());
    }

    json marker;
    try
    {
      in >> marker;
    }
    catch (const json::parse_error &)
    {
      throw spio::PublishError("registry marker is invalid JSON: " + marker_path.string());
    }

    if (!marker.is_object() || marker.value("kind", "") != "filesystem-local" || marker.value("schema_version", 0) != 1)
    {
      throw spio::PublishError("registry marker does not match the current filesystem registry contract: " + marker_path.string());
    }
    return;
  }

  WriteJsonFileAtomically(
      marker_path,
      {
          {"kind", "filesystem-local"},
          {"schema_version", 1},
      });
}

std::string CurrentUtcTimestamp()
{
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

fs::path BlobPathForDigest(const fs::path &registry_root, const std::string &digest)
{
  return registry_root / "blobs" / "sha256" / digest.substr(0U, 2U) / digest.substr(2U, 2U) / (digest + ".tar");
}

fs::path VersionEntryPath(const fs::path &registry_root, const std::string &package_name, const std::string &package_version)
{
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return registry_root / "index" / package_namespace / short_name / (package_version + ".json");
}

void CopyArchiveIfMissing(const fs::path &source_archive, const fs::path &blob_path)
{
  if (fs::exists(blob_path))
  {
    if (!fs::is_regular_file(blob_path))
    {
      throw spio::PublishError("registry blob path is not a regular file: " + blob_path.string());
    }
    return;
  }

  fs::create_directories(blob_path.parent_path());
  const fs::path temp_path = blob_path.parent_path() / (blob_path.filename().string() + ".tmp");
  std::error_code ec;
  fs::copy_file(source_archive, temp_path, fs::copy_options::overwrite_existing, ec);
  if (ec)
  {
    throw spio::PublishError("failed to stage registry blob: " + temp_path.string());
  }
  fs::rename(temp_path, blob_path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::PublishError("failed to finalize registry blob: " + blob_path.string());
  }
}

json BuildDependencyArray(const std::vector<spio::Dependency> &dependencies)
{
  json payload = json::array();
  for (const spio::Dependency &dependency : dependencies)
  {
    payload.push_back({
        {"alias", dependency.alias},
        {"package", dependency.package.value_or("")},
        {"version", dependency.version.value_or("")},
        {"registry", dependency.source},
    });
  }
  return payload;
}

json BuildVersionEntry(
    const spio::PublishResult &candidate,
    const fs::path &registry_root,
    const fs::path &blob_path,
    const std::string &archive_sha256,
    const uint64_t archive_size_bytes,
    const std::string &published_at_utc)
{
  return {
      {"schema_version", 1},
      {"package", candidate.package_name},
      {"version", candidate.package_version},
      {"sha256", archive_sha256},
      {"size_bytes", archive_size_bytes},
      {"blob_path", fs::relative(blob_path, registry_root).generic_string()},
      {"published_at", published_at_utc},
      {"dependencies", BuildDependencyArray(candidate.dependencies)},
      {"dev_dependencies", BuildDependencyArray(candidate.dev_dependencies)},
  };
}

}  // namespace

namespace spio
{

RegistryPublishResult PublishToFilesystemRegistry(const RegistryPublishRequest &request)
{
  const PublishResult candidate = PreparePublishCandidate(request.publish_request);
  const fs::path registry_root = CanonicalAbsolutePath(request.registry_root);
  EnsureFilesystemRegistryMarker(registry_root);

  const std::string archive_sha256 = spio::Sha256File(candidate.archive_path);
  const fs::path blob_path = BlobPathForDigest(registry_root, archive_sha256);
  const fs::path entry_path = VersionEntryPath(registry_root, candidate.package_name, candidate.package_version);
  if (fs::exists(entry_path))
  {
    throw PublishError("package version is already published in the target filesystem registry: " + candidate.package_name + "@" +
                       candidate.package_version);
  }

  CopyArchiveIfMissing(candidate.archive_path, blob_path);
  const std::string published_at_utc = CurrentUtcTimestamp();
  std::error_code ec;
  const uint64_t archive_size_bytes = static_cast<uint64_t>(fs::file_size(candidate.archive_path, ec));
  if (ec)
  {
    throw PublishError("failed to inspect archive size for registry publish: " + candidate.archive_path.string());
  }
  WriteJsonFileAtomically(
      entry_path,
      BuildVersionEntry(candidate, registry_root, blob_path, archive_sha256, archive_size_bytes, published_at_utc));

  return {
      .candidate = candidate,
      .registry_root = registry_root,
      .registry_marker_path = registry_root / "spio-registry.json",
      .registry_blob_path = blob_path,
      .registry_entry_path = entry_path,
      .archive_sha256 = archive_sha256,
      .archive_size_bytes = archive_size_bytes,
      .published_at_utc = published_at_utc,
  };
}

}  // namespace spio
