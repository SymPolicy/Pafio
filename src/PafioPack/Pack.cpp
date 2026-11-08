#include "PafioPack/Pack.hpp"

#include "PafioCore/AtomicFile.hpp"
#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioPack/PackageSelection.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{

struct PackEntry
{
  std::string archive_path;
  std::optional<fs::path> source_path;
  std::optional<std::string> inline_contents;
};

bool IsRelativePathPrefix(const fs::path &candidate, const fs::path &prefix)
{
  const fs::path relative = candidate.lexically_relative(prefix);
  if (relative.empty())
  {
    return false;
  }
  const std::string text = relative.generic_string();
  return text == "." || (text != ".." && !text.starts_with("../"));
}

bool IsUnderRoot(const fs::path &candidate, const fs::path &root)
{
  return IsRelativePathPrefix(pafio::CanonicalAbsolutePath(candidate), pafio::CanonicalAbsolutePath(root));
}

bool IsExcludedGeneratedTopLevel(const fs::path &relative_path)
{
  if (relative_path.empty())
  {
    return false;
  }
  const std::string first = relative_path.begin()->string();
  return first == ".git" || first == ".pafio" || first == "dist" || first == "build" || first.starts_with("build-");
}

bool IsExcludedWorkspaceSubtree(const fs::path &relative_path, const std::vector<fs::path> &excluded_roots)
{
  for (const fs::path &excluded_root : excluded_roots)
  {
    if (IsRelativePathPrefix(relative_path.lexically_normal(), excluded_root))
    {
      return true;
    }
  }
  return false;
}

// Hidden entries are local state (tooling config, editor files, credentials
// stores); they are never package payload.
bool IsHiddenEntry(const fs::path &relative_path)
{
  for (const fs::path &component : relative_path)
  {
    const std::string name = component.string();
    if (!name.empty() && name.front() == '.')
    {
      return true;
    }
  }
  return false;
}

// Names that conventionally carry credentials or local state. A non-hidden
// file matching one of these fails the pack outright so a secret can never
// ride inside a published archive.
bool IsSecretNamedFile(const fs::path &relative_path)
{
  const std::string name = relative_path.filename().string();
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (lower == ".env" || lower.starts_with(".env.") || lower == ".netrc" || lower == ".npmrc")
  {
    return true;
  }
  if (lower == "id_rsa" || lower.starts_with("id_rsa.") || lower == "id_ed25519" || lower.starts_with("id_ed25519."))
  {
    return true;
  }
  static const std::array<std::string_view, 8> kSecretExtensions = {
      ".pem", ".key", ".p12", ".pfx", ".kdbx", ".tfstate", ".tfvars", ".htpasswd"};
  for (const std::string_view ext : kSecretExtensions)
  {
    if (lower.size() > ext.size() && lower.ends_with(ext))
    {
      return true;
    }
  }
  static const std::array<std::string_view, 5> kSecretWords = {
      "secret", "credential", "password", "passwd", "token"};
  for (const std::string_view word : kSecretWords)
  {
    if (lower.find(word) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

// Credential shapes scanned over packaged file contents: a private-key block,
// a provider API token, or a structured JWT must never leave the machine
// inside a published archive.
const std::vector<std::pair<std::string_view, std::regex>> &SecretContentPatterns()
{
  static const std::vector<std::pair<std::string_view, std::regex>> patterns = {
      {"private-key-block", std::regex(R"(-----BEGIN [A-Z0-9]+ ?PRIVATE KEY-----)")},
      {"aws-access-key", std::regex(R"((AKIA|ASIA)[A-Z0-9]{16})")},
      {"github-token", std::regex(R"(gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{22,})")},
      {"anthropic-key", std::regex(R"(sk-ant-[A-Za-z0-9_-]{20,})")},
      {"google-api-key", std::regex(R"(AIza[A-Za-z0-9_-]{35})")},
      {"openai-key", std::regex(R"(sk-[A-Za-z0-9_-]{20,})")},
      {"slack-token", std::regex(R"(xox[baprs]-[A-Za-z0-9-]{10,})")},
      {"stripe-key", std::regex(R"((sk|rk)_(live|test)_[A-Za-z0-9]{20,})")},
      {"json-web-token", std::regex(R"(eyJ[A-Za-z0-9_-]{10,}[.][A-Za-z0-9_-]{10,}[.][A-Za-z0-9_-]{10,})")},
  };
  return patterns;
}

std::string ReadBinaryFile(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    throw pafio::PackError("failed to read package source file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void RejectSecretContents(const PackEntry &entry)
{
  const std::string contents =
      entry.inline_contents.has_value() ? *entry.inline_contents : ReadBinaryFile(*entry.source_path);
  for (const auto &[rule, pattern] : SecretContentPatterns())
  {
    if (std::regex_search(contents, pattern))
    {
      throw pafio::PackError(
          "pack refuses to archive credential-shaped content (" + std::string(rule) + ") in " + entry.archive_path);
    }
  }
}

std::vector<fs::path> CollectExcludedWorkspaceRoots(const pafio::PackageSelection &selection)
{
  std::vector<fs::path> excluded_roots;
  if (!selection.manifest.workspace.has_value())
  {
    return excluded_roots;
  }

  auto add_relative_path = [&](const std::string &text) {
    const fs::path relative = fs::path(text).lexically_normal();
    if (relative.empty() || relative == ".")
    {
      return;
    }
    excluded_roots.push_back(relative);
  };

  for (const std::string &member : selection.manifest.workspace->members)
  {
    add_relative_path(member);
  }
  for (const std::string &excluded : selection.manifest.workspace->exclude)
  {
    add_relative_path(excluded);
  }

  std::sort(
      excluded_roots.begin(),
      excluded_roots.end(),
      [](const fs::path &left, const fs::path &right) {
        return left.generic_string() < right.generic_string();
      });
  excluded_roots.erase(
      std::unique(
          excluded_roots.begin(),
          excluded_roots.end(),
          [](const fs::path &left, const fs::path &right) {
            return left.generic_string() == right.generic_string();
          }),
      excluded_roots.end());
  return excluded_roots;
}

std::vector<PackEntry> CollectPackEntries(const pafio::PackageSelection &selection, const fs::path &archive_path)
{
  const fs::path package_root = pafio::CanonicalAbsolutePath(selection.package_root);
  const fs::path manifest_path = pafio::CanonicalAbsolutePath(selection.manifest_path);
  const fs::path lockfile_path = package_root / "pafio.lock";
  const fs::path normalized_archive_path = pafio::CanonicalAbsolutePath(archive_path);
  if (IsUnderRoot(normalized_archive_path, package_root))
  {
    const fs::path relative_output = normalized_archive_path.lexically_relative(package_root);
    if (!IsExcludedGeneratedTopLevel(relative_output))
    {
      throw pafio::PackError(
          "archive output inside the package root must live under an excluded generated directory such as dist/: " +
          normalized_archive_path.string());
    }
  }

  std::vector<PackEntry> entries;
  entries.push_back({
      .archive_path = "pafio.toml",
      .source_path = std::nullopt,
      .inline_contents = pafio::SerializeManifestCanonical(selection.manifest),
  });

  const std::vector<fs::path> excluded_workspace_roots = CollectExcludedWorkspaceRoots(selection);
  for (fs::recursive_directory_iterator it(package_root), end; it != end; ++it)
  {
    const fs::directory_entry &entry = *it;
    const fs::path absolute_entry = pafio::CanonicalAbsolutePath(entry.path());
    const fs::path relative_entry = absolute_entry.lexically_relative(package_root);
    if (relative_entry.empty() || relative_entry == ".")
    {
      continue;
    }

    if (absolute_entry == manifest_path || absolute_entry == lockfile_path || absolute_entry == normalized_archive_path)
    {
      continue;
    }
    if (IsExcludedGeneratedTopLevel(relative_entry) || IsExcludedWorkspaceSubtree(relative_entry, excluded_workspace_roots)
        || IsHiddenEntry(relative_entry))
    {
      if (entry.is_directory())
      {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (IsSecretNamedFile(relative_entry))
    {
      throw pafio::PackError(
          "pack refuses to archive a credential or local-state file: " + relative_entry.generic_string());
    }

    const fs::file_status status = entry.symlink_status();
    if (fs::is_symlink(status))
    {
      throw pafio::PackError("pack does not support symlinks inside the package tree: " + absolute_entry.string());
    }
    if (fs::is_directory(status))
    {
      continue;
    }
    if (!fs::is_regular_file(status))
    {
      throw pafio::PackError("pack only supports regular files inside the package tree: " + absolute_entry.string());
    }

    entries.push_back({
        .archive_path = relative_entry.generic_string(),
        .source_path = absolute_entry,
        .inline_contents = std::nullopt,
    });
  }

  std::sort(
      entries.begin(),
      entries.end(),
      [](const PackEntry &left, const PackEntry &right) {
        return left.archive_path < right.archive_path;
      });
  for (const PackEntry &entry : entries)
  {
    RejectSecretContents(entry);
  }
  return entries;
}

std::string HexOctal(uint64_t value)
{
  std::ostringstream out;
  out << std::oct << value;
  return out.str();
}

void WriteOctalField(std::array<char, 512> &header, size_t offset, size_t length, uint64_t value)
{
  const std::string octal = HexOctal(value);
  if (octal.size() + 1U > length)
  {
    throw pafio::PackError("archive field exceeds ustar numeric limit");
  }

  std::fill_n(header.data() + static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(length), '0');
  const size_t digits_offset = offset + (length - 1U - octal.size());
  std::memcpy(header.data() + static_cast<std::ptrdiff_t>(digits_offset), octal.data(), octal.size());
  header[offset + length - 1U] = '\0';
}

void WriteChecksumField(std::array<char, 512> &header, uint64_t checksum)
{
  const std::string octal = HexOctal(checksum);
  if (octal.size() > 6U)
  {
    throw pafio::PackError("archive checksum exceeds ustar limit");
  }

  std::fill(header.begin() + 148, header.begin() + 156, ' ');
  const size_t digits_offset = 148U + (6U - octal.size());
  std::memcpy(header.data() + static_cast<std::ptrdiff_t>(digits_offset), octal.data(), octal.size());
  header[154] = '\0';
  header[155] = ' ';
}

void SplitArchivePathForUstar(const std::string &path, std::string &name, std::string &prefix)
{
  if (path.empty())
  {
    throw pafio::PackError("archive entry path must not be empty");
  }
  if (path.size() <= 100U)
  {
    name = path;
    prefix.clear();
    return;
  }

  size_t split = path.rfind('/');
  while (split != std::string::npos)
  {
    const std::string candidate_prefix = path.substr(0, split);
    const std::string candidate_name = path.substr(split + 1U);
    if (!candidate_prefix.empty() && candidate_prefix.size() <= 155U && !candidate_name.empty() && candidate_name.size() <= 100U)
    {
      prefix = candidate_prefix;
      name = candidate_name;
      return;
    }
    if (split == 0U)
    {
      break;
    }
    split = path.rfind('/', split - 1U);
  }

  throw pafio::PackError("archive entry exceeds ustar path limits: " + path);
}

// `write` receives (data, size) and must consume all bytes or throw; header,
// payload, and padding go straight to it so peak memory stays at one entry's
// contents instead of a second serialized copy per entry.
template <typename Write>
void WriteTarEntry(Write &&write, const std::string &archive_path, const std::string &contents)
{
  std::array<char, 512> header{};
  std::string name;
  std::string prefix;
  SplitArchivePathForUstar(archive_path, name, prefix);

  std::memcpy(header.data(), name.data(), name.size());
  if (!prefix.empty())
  {
    std::memcpy(header.data() + 345, prefix.data(), prefix.size());
  }

  WriteOctalField(header, 100, 8, 0644);
  WriteOctalField(header, 108, 8, 0);
  WriteOctalField(header, 116, 8, 0);
  WriteOctalField(header, 124, 12, contents.size());
  WriteOctalField(header, 136, 12, 0);
  std::fill(header.begin() + 148, header.begin() + 156, ' ');
  header[156] = '0';
  std::memcpy(header.data() + 257, "ustar", 5);
  header[262] = '\0';
  header[263] = '0';
  header[264] = '0';

  uint64_t checksum = 0;
  for (const unsigned char ch : header)
  {
    checksum += ch;
  }
  WriteChecksumField(header, checksum);

  write(header.data(), header.size());
  write(contents.data(), contents.size());
  const size_t remainder = contents.size() % 512U;
  if (remainder != 0U)
  {
    const std::array<char, 512> padding{};
    write(padding.data(), 512U - remainder);
  }
}

// The temporary archive is created exclusively in the destination directory so
// a pre-planted file or symlink at a predictable name cannot redirect the
// write into an unrelated file, then atomically renamed over the final path.
void WriteTarArchive(const std::vector<PackEntry> &entries, const fs::path &archive_path, std::string_view archive_prefix)
{
  fs::create_directories(archive_path.parent_path());
  pafio::ExclusiveTempFile temp;
  try
  {
    temp = pafio::OpenExclusiveTemporaryFile(archive_path);
  }
  catch (const std::system_error &error)
  {
    throw pafio::PackError("failed to create temporary package archive: " + std::string(error.what()));
  }
  const fs::path temp_path = temp.path;

  try
  {
    for (const PackEntry &entry : entries)
    {
      const std::string archive_member = std::string(archive_prefix) + "/" + entry.archive_path;
      const std::string contents = entry.inline_contents.has_value() ? *entry.inline_contents : ReadBinaryFile(*entry.source_path);
      try
      {
        WriteTarEntry(
            [&temp](const char *data, size_t size) { pafio::WriteTemporaryFileFully(temp, data, size); },
            archive_member,
            contents);
      }
      catch (const std::system_error &error)
      {
        throw pafio::PackError(
            "failed to write temporary package archive: " + temp_path.string() + ": " + error.what());
      }
    }

    const std::array<char, 1024> trailer{};
    try
    {
      pafio::WriteTemporaryFileFully(temp, trailer.data(), trailer.size());
      pafio::FlushTemporaryFile(temp);
      pafio::CloseTemporaryFile(temp);
    }
    catch (const std::system_error &error)
    {
      throw pafio::PackError(
          "failed to finish package archive: " + temp_path.string() + ": " + error.what());
    }

    fs::rename(temp_path, archive_path);
  }
  catch (...)
  {
    pafio::DiscardTemporaryFile(temp);
    std::error_code ignored;
    fs::remove(temp_path, ignored);
    throw;
  }
}

}  // namespace

namespace pafio
{

PackResult WriteSourcePackage(const PackRequest &request)
{
  const pafio::PackageSelection selection =
      pafio::SelectWorkspacePackage<pafio::PackError>(request.manifest_path, request.package_name, "pack");
  const PackageConfig &package = *selection.manifest.package;
  const std::string short_name = pafio::PackageShortName(package.name);
  const std::string archive_prefix = short_name + "-" + package.version;
  const fs::path package_root = pafio::CanonicalAbsolutePath(selection.package_root);
  const fs::path archive_path = request.output_path.has_value()
                                    ? pafio::CanonicalAbsolutePath(*request.output_path)
                                    : pafio::CanonicalAbsolutePath(package_root / "dist" / (archive_prefix + ".tar"));

  const std::vector<PackEntry> entries = CollectPackEntries(selection, archive_path);
  WriteTarArchive(entries, archive_path, archive_prefix);

  return {
      .manifest_path = pafio::CanonicalAbsolutePath(selection.manifest_path),
      .package_root = package_root,
      .archive_path = archive_path,
      .package_name = package.name,
      .package_version = package.version,
      .archive_prefix = archive_prefix,
      .file_count = entries.size(),
  };
}

}  // namespace pafio
