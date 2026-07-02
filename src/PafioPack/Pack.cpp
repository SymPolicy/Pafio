#include "PafioPack/Pack.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioManifest/Manifest.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{

struct PackageSelection
{
  fs::path manifest_path;
  fs::path package_root;
  pafio::ManifestDocument manifest;
};

struct PackEntry
{
  std::string archive_path;
  std::optional<fs::path> source_path;
  std::optional<std::string> inline_contents;
};

fs::path CanonicalAbsolutePath(const fs::path &path)
{
  return fs::absolute(path).lexically_normal();
}

std::string PackageShortName(const std::string &package_name)
{
  const size_t slash = package_name.find('/');
  if (slash == std::string::npos || slash + 1U >= package_name.size())
  {
    throw pafio::ValidationError("package.name must match namespace/name");
  }
  return package_name.substr(slash + 1U);
}

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
  return IsRelativePathPrefix(CanonicalAbsolutePath(candidate), CanonicalAbsolutePath(root));
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

std::vector<PackageSelection> CollectPackageCandidates(const fs::path &root_manifest_path)
{
  const fs::path normalized_root_manifest = CanonicalAbsolutePath(root_manifest_path);
  const fs::path root_dir = normalized_root_manifest.parent_path();
  const pafio::ManifestDocument root_manifest = pafio::LoadManifest(normalized_root_manifest);

  std::vector<PackageSelection> candidates;
  if (root_manifest.package.has_value())
  {
    candidates.push_back({
        .manifest_path = normalized_root_manifest,
        .package_root = root_dir,
        .manifest = root_manifest,
    });
  }

  if (root_manifest.workspace.has_value())
  {
    for (const std::string &member : root_manifest.workspace->members)
    {
      const fs::path member_manifest_path = CanonicalAbsolutePath(root_dir / member / "pafio.toml");
      const pafio::ManifestDocument member_manifest = pafio::LoadManifest(member_manifest_path);
      if (!member_manifest.package.has_value())
      {
        throw pafio::WorkspaceError("workspace member manifest must define [package]: " + member_manifest_path.string());
      }
      candidates.push_back({
          .manifest_path = member_manifest_path,
          .package_root = member_manifest_path.parent_path(),
          .manifest = member_manifest,
      });
    }
  }

  return candidates;
}

PackageSelection SelectPackageForPack(const pafio::PackRequest &request)
{
  const fs::path root_manifest_path = CanonicalAbsolutePath(request.manifest_path);
  const std::vector<PackageSelection> candidates = CollectPackageCandidates(root_manifest_path);
  if (candidates.empty())
  {
    throw pafio::PackError("pack requires a manifest with a local [package] target");
  }

  if (request.package_name.has_value())
  {
    std::vector<const PackageSelection *> matches;
    for (const PackageSelection &candidate : candidates)
    {
      if (candidate.manifest.package->name == *request.package_name)
      {
        matches.push_back(&candidate);
      }
    }
    if (matches.empty())
    {
      throw pafio::PackError("selected package was not found under the active manifest: " + *request.package_name);
    }
    if (matches.size() > 1U)
    {
      throw pafio::PackError("selected package is ambiguous under the active manifest: " + *request.package_name);
    }
    return *matches.front();
  }

  for (const PackageSelection &candidate : candidates)
  {
    if (candidate.manifest_path == root_manifest_path && candidate.manifest.package.has_value())
    {
      return candidate;
    }
  }

  if (candidates.size() == 1U)
  {
    return candidates.front();
  }

  throw pafio::PackError("workspace pack is ambiguous; select a root package with --package <namespace/name>");
}

std::vector<fs::path> CollectExcludedWorkspaceRoots(const PackageSelection &selection)
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

std::vector<PackEntry> CollectPackEntries(const PackageSelection &selection, const fs::path &archive_path)
{
  const fs::path package_root = CanonicalAbsolutePath(selection.package_root);
  const fs::path manifest_path = CanonicalAbsolutePath(selection.manifest_path);
  const fs::path lockfile_path = package_root / "pafio.lock";
  const fs::path normalized_archive_path = CanonicalAbsolutePath(archive_path);
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
    const fs::path absolute_entry = CanonicalAbsolutePath(entry.path());
    const fs::path relative_entry = absolute_entry.lexically_relative(package_root);
    if (relative_entry.empty() || relative_entry == ".")
    {
      continue;
    }

    if (absolute_entry == manifest_path || absolute_entry == lockfile_path || absolute_entry == normalized_archive_path)
    {
      continue;
    }
    if (IsExcludedGeneratedTopLevel(relative_entry) || IsExcludedWorkspaceSubtree(relative_entry, excluded_workspace_roots))
    {
      if (entry.is_directory())
      {
        it.disable_recursion_pending();
      }
      continue;
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

void WriteTarEntry(std::ostream &out, const std::string &archive_path, const std::string &contents)
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

  out.write(header.data(), static_cast<std::streamsize>(header.size()));
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  const size_t remainder = contents.size() % 512U;
  if (remainder != 0U)
  {
    const std::array<char, 512> padding{};
    out.write(padding.data(), static_cast<std::streamsize>(512U - remainder));
  }
}

void WriteTarArchive(const std::vector<PackEntry> &entries, const fs::path &archive_path, std::string_view archive_prefix)
{
  const fs::path temp_path = archive_path.string() + ".tmp";
  fs::create_directories(archive_path.parent_path());

  try
  {
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      throw pafio::PackError("failed to create package archive: " + temp_path.string());
    }

    for (const PackEntry &entry : entries)
    {
      const std::string archive_member = std::string(archive_prefix) + "/" + entry.archive_path;
      const std::string contents = entry.inline_contents.has_value() ? *entry.inline_contents : ReadBinaryFile(*entry.source_path);
      WriteTarEntry(out, archive_member, contents);
    }

    const std::array<char, 1024> trailer{};
    out.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
    if (!out.good())
    {
      throw pafio::PackError("failed to finish package archive: " + temp_path.string());
    }
    out.close();
    if (!out)
    {
      throw pafio::PackError("failed to flush package archive: " + temp_path.string());
    }

    fs::rename(temp_path, archive_path);
  }
  catch (...)
  {
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
  const PackageSelection selection = SelectPackageForPack(request);
  const PackageConfig &package = *selection.manifest.package;
  const std::string short_name = PackageShortName(package.name);
  const std::string archive_prefix = short_name + "-" + package.version;
  const fs::path package_root = CanonicalAbsolutePath(selection.package_root);
  const fs::path archive_path = request.output_path.has_value()
                                    ? CanonicalAbsolutePath(*request.output_path)
                                    : CanonicalAbsolutePath(package_root / "dist" / (archive_prefix + ".tar"));

  const std::vector<PackEntry> entries = CollectPackEntries(selection, archive_path);
  WriteTarArchive(entries, archive_path, archive_prefix);

  return {
      .manifest_path = CanonicalAbsolutePath(selection.manifest_path),
      .package_root = package_root,
      .archive_path = archive_path,
      .package_name = package.name,
      .package_version = package.version,
      .archive_prefix = archive_prefix,
      .file_count = entries.size(),
  };
}

}  // namespace pafio
