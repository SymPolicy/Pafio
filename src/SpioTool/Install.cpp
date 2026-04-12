#include "SpioTool/Install.hpp"

#include "SpioCompat/Compat.hpp"
#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioManifest/Manifest.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

void EnsureLocalExecutableFile(const fs::path &path)
{
  if (!fs::exists(path))
  {
    throw spio::ToolError("styio binary not found: " + path.string());
  }
  if (!fs::is_regular_file(path))
  {
    throw spio::ToolError("styio binary must be a regular file: " + path.string());
  }
}

void CopyExecutableFile(const fs::path &source, const fs::path &destination)
{
  fs::create_directories(destination.parent_path());
  if (spio::CanonicalAbsolutePath(source) != spio::CanonicalAbsolutePath(destination))
  {
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
  }
  fs::permissions(destination, fs::status(source).permissions(), fs::perm_options::replace);
}

void WriteJsonFile(const fs::path &path, const json &payload)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out)
  {
    throw spio::ToolError("failed to open tool metadata for write: " + path.string());
  }
  out << payload.dump(2) << '\n';
  if (!out.good())
  {
    throw spio::ToolError("failed to write tool metadata: " + path.string());
  }
}

void WriteTextFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out)
  {
    throw spio::ToolError("failed to open tool configuration for write: " + path.string());
  }
  out << content;
  if (!out.good())
  {
    throw spio::ToolError("failed to write tool configuration: " + path.string());
  }
}

json BuildInstallMetadata(const spio::CompatibilityReport &report, const fs::path &source_binary, const fs::path &managed_binary)
{
  return {
      {"tool", "styio"},
      {"install_kind", "local-executable"},
      {"source_binary", source_binary.string()},
      {"managed_binary", managed_binary.string()},
      {"compiler_version", report.compiler_version},
      {"channel", report.compiler_channel},
      {"edition_max", report.compiler_edition_max},
      {"integration_phase", report.integration_phase},
      {"supported_compile_plan_versions", report.supported_compile_plan_versions},
      {"capabilities", report.capabilities},
  };
}

void RefreshManagedCurrentRoot(
    const spio::CompatibilityReport &report,
    const fs::path &source_binary,
    const fs::path &current_root)
{
  const fs::path managed_binary_path = spio::ManagedStyioBinaryPath(current_root);
  const fs::path current_metadata_path = spio::ManagedStyioMetadataPath(current_root);
  CopyExecutableFile(source_binary, managed_binary_path);
  WriteJsonFile(current_metadata_path, BuildInstallMetadata(report, source_binary, managed_binary_path));
}

struct InstalledManagedCompiler
{
  fs::path install_root;
  fs::path install_binary_path;
  fs::path install_metadata_path;
  std::string compiler_version;
  std::string compiler_channel;
};

std::vector<InstalledManagedCompiler> CollectInstalledManagedCompilers(const fs::path &spio_home)
{
  const fs::path styio_root = spio::ManagedStyioRoot(spio_home);
  std::vector<InstalledManagedCompiler> installed;
  if (!fs::exists(styio_root))
  {
    return installed;
  }

  for (const fs::directory_entry &channel_entry : fs::directory_iterator(styio_root))
  {
    if (!channel_entry.is_directory())
    {
      continue;
    }
    const std::string channel = channel_entry.path().filename().string();
    if (channel == "current")
    {
      continue;
    }

    for (const fs::directory_entry &version_entry : fs::directory_iterator(channel_entry.path()))
    {
      if (!version_entry.is_directory())
      {
        continue;
      }
      const fs::path install_root = version_entry.path();
      const fs::path install_binary_path = spio::ManagedStyioBinaryPath(install_root);
      const fs::path install_metadata_path = spio::ManagedStyioMetadataPath(install_root);
      if (!fs::exists(install_binary_path) || !fs::is_regular_file(install_binary_path))
      {
        continue;
      }
      if (!fs::exists(install_metadata_path) || !fs::is_regular_file(install_metadata_path))
      {
        continue;
      }
      installed.push_back({
          .install_root = install_root,
          .install_binary_path = install_binary_path,
          .install_metadata_path = install_metadata_path,
          .compiler_version = version_entry.path().filename().string(),
          .compiler_channel = channel,
      });
    }
  }

  return installed;
}

InstalledManagedCompiler SelectInstalledManagedCompiler(const fs::path &spio_home, const spio::ToolUseRequest &request)
{
  if (request.compiler_version.empty())
  {
    throw spio::ToolError("tool use requires a non-empty --version <compiler-version>");
  }
  if (request.compiler_channel.has_value() && request.compiler_channel->empty())
  {
    throw spio::ToolError("tool use --channel must be a non-empty string");
  }

  std::vector<InstalledManagedCompiler> matches;
  for (const InstalledManagedCompiler &candidate : CollectInstalledManagedCompilers(spio_home))
  {
    if (candidate.compiler_version != request.compiler_version)
    {
      continue;
    }
    if (request.compiler_channel.has_value() && candidate.compiler_channel != *request.compiler_channel)
    {
      continue;
    }
    matches.push_back(candidate);
  }

  if (matches.empty())
  {
    throw spio::ToolError(
        "managed styio compiler is not installed: " +
        (request.compiler_channel.has_value()
             ? *request.compiler_channel + "/" + request.compiler_version
             : request.compiler_version));
  }
  if (matches.size() > 1U)
  {
    throw spio::ToolError(
        "managed styio compiler version is ambiguous across installed channels; select --channel <channel>: " +
        request.compiler_version);
  }
  return matches.front();
}

std::string RenderProjectToolchainPin(const InstalledManagedCompiler &selected)
{
  return "[styio]\n"
         "channel = \"" + selected.compiler_channel + "\"\n"
         "version = \"" + selected.compiler_version + "\"\n";
}

}  // namespace

namespace spio
{

ToolInstallResult InstallManagedStyio(const ToolInstallRequest &request)
{
  const fs::path source_binary = CanonicalAbsolutePath(request.styio_binary);
  EnsureLocalExecutableFile(source_binary);

  const CompatibilityReport report = CheckCompilerCompatibility(source_binary);
  const fs::path spio_home = ResolveSpioHome();

  const fs::path install_root = ManagedStyioInstallRoot(spio_home, report.compiler_channel, report.compiler_version);
  const fs::path install_binary_path = ManagedStyioBinaryPath(install_root);
  const fs::path install_metadata_path = ManagedStyioMetadataPath(install_root);

  const fs::path current_root = ManagedStyioCurrentRoot(spio_home);
  const fs::path managed_binary_path = ManagedStyioBinaryPath(current_root);
  const fs::path current_metadata_path = ManagedStyioMetadataPath(current_root);

  CopyExecutableFile(source_binary, install_binary_path);
  WriteJsonFile(install_metadata_path, BuildInstallMetadata(report, source_binary, install_binary_path));

  RefreshManagedCurrentRoot(report, install_binary_path, current_root);

  return ToolInstallResult{
      .source_binary = source_binary,
      .spio_home = spio_home,
      .install_root = install_root,
      .install_binary_path = install_binary_path,
      .install_metadata_path = install_metadata_path,
      .current_root = current_root,
      .managed_binary_path = managed_binary_path,
      .current_metadata_path = current_metadata_path,
      .compiler_version = report.compiler_version,
      .compiler_channel = report.compiler_channel,
      .compiler_edition_max = report.compiler_edition_max,
      .integration_phase = report.integration_phase,
      .supported_compile_plan_versions = report.supported_compile_plan_versions,
      .capabilities = report.capabilities,
  };
}

ToolUseResult UseManagedStyio(const ToolUseRequest &request)
{
  const fs::path spio_home = ResolveSpioHome();
  const InstalledManagedCompiler selected = SelectInstalledManagedCompiler(spio_home, request);

  const CompatibilityReport report = CheckCompilerCompatibility(selected.install_binary_path);
  const fs::path current_root = ManagedStyioCurrentRoot(spio_home);
  RefreshManagedCurrentRoot(report, selected.install_binary_path, current_root);

  return ToolUseResult{
      .spio_home = spio_home,
      .install_root = selected.install_root,
      .install_binary_path = selected.install_binary_path,
      .install_metadata_path = selected.install_metadata_path,
      .current_root = current_root,
      .managed_binary_path = ManagedStyioBinaryPath(current_root),
      .current_metadata_path = ManagedStyioMetadataPath(current_root),
      .compiler_version = report.compiler_version,
      .compiler_channel = report.compiler_channel,
      .compiler_edition_max = report.compiler_edition_max,
      .integration_phase = report.integration_phase,
      .supported_compile_plan_versions = report.supported_compile_plan_versions,
      .capabilities = report.capabilities,
  };
}

ToolPinResult PinManagedStyio(const ToolPinRequest &request)
{
  const fs::path manifest_path = CanonicalAbsolutePath(request.manifest_path);
  (void) LoadManifest(manifest_path);

  const fs::path pin_path = ProjectToolchainPinPathForManifest(manifest_path);
  if (request.clear)
  {
    std::error_code ec;
    fs::remove(pin_path, ec);
    if (ec)
    {
      throw ToolError("failed to clear project toolchain pin: " + pin_path.string());
    }

    return ToolPinResult{
        .manifest_path = manifest_path,
        .pin_path = pin_path,
        .cleared = true,
    };
  }

  if (!request.compiler_version.has_value() || request.compiler_version->empty())
  {
    throw ToolError("tool pin requires --version <compiler-version> unless --clear is set");
  }
  if (request.compiler_channel.has_value() && request.compiler_channel->empty())
  {
    throw ToolError("tool pin --channel must be a non-empty string");
  }

  const fs::path spio_home = ResolveSpioHome();
  const InstalledManagedCompiler selected = SelectInstalledManagedCompiler(
      spio_home,
      ToolUseRequest{
          .compiler_version = *request.compiler_version,
          .compiler_channel = request.compiler_channel,
      });
  (void) CheckCompilerCompatibility(selected.install_binary_path);
  WriteTextFile(pin_path, RenderProjectToolchainPin(selected));

  return ToolPinResult{
      .manifest_path = manifest_path,
      .pin_path = pin_path,
      .cleared = false,
      .install_root = selected.install_root,
      .install_binary_path = selected.install_binary_path,
      .compiler_version = selected.compiler_version,
      .compiler_channel = selected.compiler_channel,
  };
}

}  // namespace spio
