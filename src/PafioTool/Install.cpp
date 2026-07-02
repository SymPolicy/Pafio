#include "PafioTool/Install.hpp"

#include "PafioCompat/Compat.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioManifest/Manifest.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

void EnsureLocalExecutableFile(const fs::path &path)
{
  if (!fs::exists(path))
  {
    throw pafio::ToolError("styio binary not found: " + path.string());
  }
  if (!fs::is_regular_file(path))
  {
    throw pafio::ToolError("styio binary must be a regular file: " + path.string());
  }
}

void CopyExecutableFile(const fs::path &source, const fs::path &destination)
{
  fs::create_directories(destination.parent_path());
  if (pafio::CanonicalAbsolutePath(source) != pafio::CanonicalAbsolutePath(destination))
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
    throw pafio::ToolError("failed to open tool metadata for write: " + path.string());
  }
  out << payload.dump(2) << '\n';
  if (!out.good())
  {
    throw pafio::ToolError("failed to write tool metadata: " + path.string());
  }
}

void WriteTextFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out)
  {
    throw pafio::ToolError("failed to open tool configuration for write: " + path.string());
  }
  out << content;
  if (!out.good())
  {
    throw pafio::ToolError("failed to write tool configuration: " + path.string());
  }
}

json BuildInstallMetadata(const pafio::CompatibilityReport &report, const fs::path &source_binary, const fs::path &managed_binary)
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

json LoadJsonFile(const fs::path &path)
{
  std::ifstream in(path);
  if (!in)
  {
    throw pafio::ToolError("failed to open tool metadata for read: " + path.string());
  }
  json payload;
  in >> payload;
  if (!in.good() && !in.eof())
  {
    throw pafio::ToolError("failed to parse tool metadata JSON: " + path.string());
  }
  return payload;
}

pafio::ManagedToolchainStatus BuildManagedToolchainStatus(
    const fs::path &install_root,
    const fs::path &install_binary_path,
    const fs::path &install_metadata_path,
    bool current)
{
  const json metadata = LoadJsonFile(install_metadata_path);
  return pafio::ManagedToolchainStatus{
      .install_root = install_root,
      .install_binary_path = install_binary_path,
      .install_metadata_path = install_metadata_path,
      .compiler_version = metadata.value("compiler_version", install_root.filename().string()),
      .compiler_channel = metadata.value("channel", install_root.parent_path().filename().string()),
      .compiler_edition_max = metadata.value("edition_max", ""),
      .integration_phase = metadata.value("integration_phase", ""),
      .supported_compile_plan_versions = metadata.value("supported_compile_plan_versions", std::vector<int>{}),
      .capabilities = metadata.value("capabilities", std::vector<std::string>{}),
      .current = current,
  };
}

void RefreshManagedCurrentRoot(
    const pafio::CompatibilityReport &report,
    const fs::path &source_binary,
    const fs::path &current_root)
{
  const fs::path managed_binary_path = pafio::ManagedStyioBinaryPath(current_root);
  const fs::path current_metadata_path = pafio::ManagedStyioMetadataPath(current_root);
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

std::vector<InstalledManagedCompiler> CollectInstalledManagedCompilers(const fs::path &pafio_home)
{
  const fs::path styio_root = pafio::ManagedStyioRoot(pafio_home);
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
      const fs::path install_binary_path = pafio::ManagedStyioBinaryPath(install_root);
      const fs::path install_metadata_path = pafio::ManagedStyioMetadataPath(install_root);
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

std::optional<pafio::ProjectToolchainPinStatus> LoadProjectToolchainPinStatus(const std::optional<fs::path> &manifest_path)
{
  if (!manifest_path.has_value())
  {
    return std::nullopt;
  }

  const std::optional<fs::path> pin_path = pafio::FindProjectToolchainPinPath(*manifest_path);
  if (!pin_path.has_value())
  {
    return std::nullopt;
  }

  toml::table doc;
  try
  {
    doc = toml::parse_file(pin_path->string());
  }
  catch (const toml::parse_error &err)
  {
    throw pafio::ToolError("failed to parse project toolchain pin '" + pin_path->string() + "': " + std::string(err.description()));
  }

  const toml::table *styio_table = doc["styio"].as_table();
  if (styio_table == nullptr)
  {
    throw pafio::ToolError("project toolchain pin is missing [styio]: " + pin_path->string());
  }

  const auto version = styio_table->get_as<std::string>("version");
  const auto channel = styio_table->get_as<std::string>("channel");
  if (version == nullptr || version->get().empty())
  {
    throw pafio::ToolError("project toolchain pin is missing styio.version: " + pin_path->string());
  }
  if (channel == nullptr || channel->get().empty())
  {
    throw pafio::ToolError("project toolchain pin is missing styio.channel: " + pin_path->string());
  }

  return pafio::ProjectToolchainPinStatus{
      .pin_path = *pin_path,
      .compiler_version = version->get(),
      .compiler_channel = channel->get(),
  };
}

InstalledManagedCompiler SelectInstalledManagedCompiler(const fs::path &pafio_home, const pafio::ToolUseRequest &request)
{
  if (request.compiler_version.empty())
  {
    throw pafio::ToolError("tool use requires a non-empty --version <compiler-version>");
  }
  if (request.compiler_channel.has_value() && request.compiler_channel->empty())
  {
    throw pafio::ToolError("tool use --channel must be a non-empty string");
  }

  std::vector<InstalledManagedCompiler> matches;
  for (const InstalledManagedCompiler &candidate : CollectInstalledManagedCompilers(pafio_home))
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
    throw pafio::ToolError(
        "managed styio compiler is not installed: " +
        (request.compiler_channel.has_value()
             ? *request.compiler_channel + "/" + request.compiler_version
             : request.compiler_version));
  }
  if (matches.size() > 1U)
  {
    throw pafio::ToolError(
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

namespace pafio
{

ToolInstallResult InstallManagedStyio(const ToolInstallRequest &request)
{
  const fs::path source_binary = CanonicalAbsolutePath(request.styio_binary);
  EnsureLocalExecutableFile(source_binary);

  const CompatibilityReport report = CheckCompilerCompatibility(source_binary);
  const fs::path pafio_home = ResolvePafioHome();

  const fs::path install_root = ManagedStyioInstallRoot(pafio_home, report.compiler_channel, report.compiler_version);
  const fs::path install_binary_path = ManagedStyioBinaryPath(install_root);
  const fs::path install_metadata_path = ManagedStyioMetadataPath(install_root);

  const fs::path current_root = ManagedStyioCurrentRoot(pafio_home);
  const fs::path managed_binary_path = ManagedStyioBinaryPath(current_root);
  const fs::path current_metadata_path = ManagedStyioMetadataPath(current_root);

  CopyExecutableFile(source_binary, install_binary_path);
  WriteJsonFile(install_metadata_path, BuildInstallMetadata(report, source_binary, install_binary_path));

  RefreshManagedCurrentRoot(report, install_binary_path, current_root);

  return ToolInstallResult{
      .source_binary = source_binary,
      .pafio_home = pafio_home,
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
  const fs::path pafio_home = ResolvePafioHome();
  const InstalledManagedCompiler selected = SelectInstalledManagedCompiler(pafio_home, request);

  const CompatibilityReport report = CheckCompilerCompatibility(selected.install_binary_path);
  const fs::path current_root = ManagedStyioCurrentRoot(pafio_home);
  RefreshManagedCurrentRoot(report, selected.install_binary_path, current_root);

  return ToolUseResult{
      .pafio_home = pafio_home,
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

  const fs::path pafio_home = ResolvePafioHome();
  const InstalledManagedCompiler selected = SelectInstalledManagedCompiler(
      pafio_home,
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

ToolStatusResult QueryToolStatus(const std::optional<fs::path> &manifest_path)
{
  const fs::path pafio_home = ResolvePafioHome();
  ToolStatusResult result{
      .pafio_home = pafio_home,
  };
  if (manifest_path.has_value())
  {
    result.manifest_path = CanonicalAbsolutePath(*manifest_path);
  }
  result.project_pin = LoadProjectToolchainPinStatus(result.manifest_path);

  if (const fs::path current_root = ManagedStyioCurrentRoot(pafio_home);
      fs::exists(ManagedStyioMetadataPath(current_root)) && fs::exists(ManagedStyioBinaryPath(current_root)))
  {
    result.current_compiler = BuildManagedToolchainStatus(
        current_root,
        ManagedStyioBinaryPath(current_root),
        ManagedStyioMetadataPath(current_root),
        true);
  }

  for (const InstalledManagedCompiler &candidate : CollectInstalledManagedCompilers(pafio_home))
  {
    const bool current = result.current_compiler.has_value() &&
                         result.current_compiler->compiler_version == candidate.compiler_version &&
                         result.current_compiler->compiler_channel == candidate.compiler_channel;
    result.managed_toolchains.push_back(
        BuildManagedToolchainStatus(
            candidate.install_root,
            candidate.install_binary_path,
            candidate.install_metadata_path,
            current));
  }

  return result;
}

}  // namespace pafio
