#include "SpioRegistryClient/Client.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Sha256.hpp"
#include "SpioSecurity/RegistrySecurity.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

struct ChildProcessResult
{
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

void CloseFd(int &fd)
{
  if (fd >= 0)
  {
    close(fd);
    fd = -1;
  }
}

bool SetNonBlocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void DrainReadablePipe(int &fd, std::string &text)
{
  std::array<char, 4096> buffer{};
  while (fd >= 0)
  {
    const ssize_t read_size = read(fd, buffer.data(), buffer.size());
    if (read_size > 0)
    {
      text.append(buffer.data(), static_cast<size_t>(read_size));
      continue;
    }
    if (read_size == 0)
    {
      CloseFd(fd);
      return;
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return;
    }
    CloseFd(fd);
    return;
  }
}

void ReapChildBestEffort(const pid_t child)
{
  int status = 0;
  while (waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      return;
    }
  }
}

int WaitForChildExit(const pid_t child)
{
  int status = 0;
  while (waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      return 1;
    }
  }
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }
  return 1;
}

ChildProcessResult RunChildProcess(const std::string &binary, const std::vector<std::string> &args)
{
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    throw spio::CacheError("failed to create pipes for registry process execution");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    throw spio::CacheError("failed to fork registry process");
  }

  if (child == 0)
  {
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(binary.c_str(), argv.data());
    _exit(127);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  if (!SetNonBlocking(stdout_pipe[0]) || !SetNonBlocking(stderr_pipe[0]))
  {
    CloseFd(stdout_pipe[0]);
    CloseFd(stderr_pipe[0]);
    ReapChildBestEffort(child);
    throw spio::CacheError("failed to configure registry process pipes");
  }

  ChildProcessResult result;
  std::array<pollfd, 2> poll_fds{};
  while (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0)
  {
    nfds_t poll_count = 0;
    int stdout_index = -1;
    int stderr_index = -1;
    if (stdout_pipe[0] >= 0)
    {
      stdout_index = static_cast<int>(poll_count);
      poll_fds[poll_count++] = pollfd{stdout_pipe[0], static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    }
    if (stderr_pipe[0] >= 0)
    {
      stderr_index = static_cast<int>(poll_count);
      poll_fds[poll_count++] = pollfd{stderr_pipe[0], static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    }

    int ready = 0;
    while ((ready = poll(poll_fds.data(), poll_count, -1)) < 0)
    {
      if (errno != EINTR)
      {
        CloseFd(stdout_pipe[0]);
        CloseFd(stderr_pipe[0]);
        ReapChildBestEffort(child);
        throw spio::CacheError("failed to read registry process output");
      }
    }
    if (ready == 0)
    {
      continue;
    }
    if (stdout_index >= 0 && poll_fds[stdout_index].revents != 0)
    {
      DrainReadablePipe(stdout_pipe[0], result.stdout_text);
    }
    if (stderr_index >= 0 && poll_fds[stderr_index].revents != 0)
    {
      DrainReadablePipe(stderr_pipe[0], result.stderr_text);
    }
  }
  result.exit_code = WaitForChildExit(child);
  return result;
}

std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
}

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
  const size_t slash = package_name.find('/');
  if (slash == std::string::npos || slash == 0U || slash + 1U >= package_name.size())
  {
    throw spio::FetchError("registry package name must match namespace/name: " + package_name);
  }
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

void ValidateMarker(const json &marker, const std::string &context)
{
  if (marker.value("kind", "") != "filesystem-local" || marker.value("schema_version", 0) != 1)
  {
    throw spio::FetchError(context + " does not match the supported registry marker contract");
  }
}

struct RegistryEntry
{
  std::string package;
  std::string version;
  std::string sha256;
  std::string blob_path;
};

RegistryEntry ValidateEntry(const json &entry, const std::string &expected_package, const std::string &expected_version)
{
  const std::string package = entry.value("package", "");
  const std::string version = entry.value("version", "");
  const std::string sha256 = entry.value("sha256", "");
  const std::string blob_path = entry.value("blob_path", "");

  if (package != expected_package)
  {
    throw spio::FetchError("registry entry package mismatch: expected '" + expected_package + "' but found '" + package + "'");
  }
  if (version != expected_version)
  {
    throw spio::FetchError("registry entry version mismatch: expected '" + expected_version + "' but found '" + version + "'");
  }
  if (sha256.size() != 64U)
  {
    throw spio::FetchError("registry entry is missing a valid sha256 digest");
  }
  if (blob_path.empty())
  {
    throw spio::FetchError("registry entry is missing blob_path");
  }

  return {
      .package = package,
      .version = version,
      .sha256 = sha256,
      .blob_path = blob_path,
  };
}

std::string FetchUrlToString(const std::string &url, const std::vector<std::string> &request_headers)
{
  std::vector<std::string> args{"-fsSL"};
  for (const std::string &header : request_headers)
  {
    args.push_back("-H");
    args.push_back(header);
  }
  args.push_back(url);
  const ChildProcessResult result = RunChildProcess("curl", args);
  if (result.exit_code != 0)
  {
    throw spio::FetchError(
        "failed to fetch registry url '" + url + "': " +
        TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text));
  }
  return result.stdout_text;
}

void FetchUrlToFile(const std::string &url, const fs::path &path, const std::vector<std::string> &request_headers)
{
  fs::create_directories(path.parent_path());
  const fs::path temp_path = path.parent_path() / (path.filename().string() + ".tmp");
  std::vector<std::string> args{"-fsSL"};
  for (const std::string &header : request_headers)
  {
    args.push_back("-H");
    args.push_back(header);
  }
  args.push_back("-o");
  args.push_back(temp_path.string());
  args.push_back(url);
  const ChildProcessResult result = RunChildProcess("curl", args);
  if (result.exit_code != 0)
  {
    std::error_code ignored;
    fs::remove(temp_path, ignored);
    throw spio::FetchError(
        "failed to download registry blob '" + url + "': " +
        TrimTrailingNewline(result.stderr_text.empty() ? result.stdout_text : result.stderr_text));
  }
  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::CacheError("failed to finalize registry blob cache: " + path.string());
  }
}

std::string JoinUrl(const std::string &root, const std::string &relative_path)
{
  return NormalizeRegistryRoot(root) + "/" + relative_path;
}

std::string MarkerUrl(const std::string &registry_root)
{
  return JoinUrl(registry_root, "spio-registry.json");
}

std::string EntryRelativePath(const std::string &package_name, const std::string &version)
{
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return "index/" + package_namespace + "/" + short_name + "/" + version + ".json";
}

fs::path EntryCachePath(const fs::path &spio_home, const std::string &registry_root, const std::string &package_name, const std::string &version)
{
  const auto [package_namespace, short_name] = SplitPackageName(package_name);
  return spio::RegistryIndexCacheRoot(spio_home) / Hex64(Fnv1a64(registry_root)) / package_namespace / short_name / (version + ".json");
}

fs::path MarkerCachePath(const fs::path &spio_home, const std::string &registry_root)
{
  return spio::RegistryIndexCacheRoot(spio_home) / Hex64(Fnv1a64(registry_root)) / "marker.json";
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

std::string LoadMarker(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const bool offline)
{
  if (IsFileRegistry(registry_root))
  {
    return ReadTextFile(FileUrlToPath(registry_root) / "spio-registry.json", "registry marker");
  }
  if (!IsHttpRegistry(registry_root))
  {
    throw spio::FetchError("registry root must use file://, http://, or https://: " + registry_root);
  }
  const fs::path marker_cache_path = MarkerCachePath(spio_home, registry_root);
  if (offline)
  {
    if (!fs::exists(marker_cache_path))
    {
      throw spio::FetchError("offline mode is missing cached registry marker for " + registry_root);
    }
    return ReadTextFile(marker_cache_path, "cached registry marker");
  }
  const std::string text = FetchUrlToString(MarkerUrl(registry_root), request_headers);
  WriteFileAtomically(marker_cache_path, text, "registry marker cache");
  return text;
}

RegistryEntry LoadEntry(
    const fs::path &spio_home,
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const std::string &package_name,
    const std::string &version,
    const bool offline)
{
  const std::string entry_relative_path = EntryRelativePath(package_name, version);
  const fs::path entry_cache_path = EntryCachePath(spio_home, registry_root, package_name, version);

  std::string text;
  if (IsFileRegistry(registry_root))
  {
    text = ReadTextFile(FileUrlToPath(registry_root) / entry_relative_path, "registry entry");
  }
  else
  {
    if (offline)
    {
      if (!fs::exists(entry_cache_path))
      {
        throw spio::FetchError("offline mode is missing cached registry entry for " + package_name + "@" + version);
      }
      text = ReadTextFile(entry_cache_path, "cached registry entry");
    }
    else
    {
      text = FetchUrlToString(JoinUrl(registry_root, entry_relative_path), request_headers);
      WriteFileAtomically(entry_cache_path, text, "registry entry cache");
    }
  }

  return ValidateEntry(ParseJsonObject(text, "registry entry"), package_name, version);
}

void MaterializeBlob(
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
      throw spio::CacheError("cached registry blob sha256 mismatch: " + blob_cache_path.string());
    }
    return;
  }

  if (IsFileRegistry(registry_root))
  {
    const fs::path source_blob = FileUrlToPath(registry_root) / entry.blob_path;
    if (!fs::exists(source_blob))
    {
      throw spio::FetchError("registry blob not found: " + source_blob.string());
    }
    fs::create_directories(blob_cache_path.parent_path());
    std::error_code ec;
    fs::copy_file(source_blob, blob_cache_path, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      throw spio::CacheError("failed to cache registry blob: " + blob_cache_path.string());
    }
  }
  else
  {
    if (offline)
    {
      throw spio::FetchError("offline mode is missing cached registry blob for " + entry.package + "@" + entry.version);
    }
    FetchUrlToFile(JoinUrl(registry_root, entry.blob_path), blob_cache_path, request_headers);
  }

  const std::string actual_sha256 = spio::Sha256File(blob_cache_path);
  if (actual_sha256 != entry.sha256)
  {
    throw spio::FetchError("registry blob sha256 mismatch for " + entry.package + "@" + entry.version);
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
  const ChildProcessResult extract = RunChildProcess("tar", {"-xf", blob_cache_path.string(), "-C", checkout_root.string()});
  if (extract.exit_code != 0)
  {
    throw spio::CacheError(
        "failed to extract registry blob '" + blob_cache_path.string() + "': " +
        TrimTrailingNewline(extract.stderr_text.empty() ? extract.stdout_text : extract.stderr_text));
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

  ValidateMarker(
      ParseJsonObject(LoadMarker(spio_home, security.registry_root, security.request_headers, offline), "registry marker"),
      "registry marker");
  const RegistryEntry entry =
      LoadEntry(spio_home, security.registry_root, security.request_headers, package_name, version, offline);
  MaterializeBlob(spio_home, security.registry_root, security.request_headers, entry, offline);
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
