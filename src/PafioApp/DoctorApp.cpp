#include "PafioApp/DoctorApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCompat/Compat.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioSecurity/RegistryTrust.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

struct DoctorCheck
{
  std::string name;
  std::string status;
  std::string message;
  json detail = json::object();
};

json SerializeCheck(const DoctorCheck &check)
{
  json result = {
      {"message", check.message},
      {"name", check.name},
      {"status", check.status},
  };
  if (!check.detail.empty())
  {
    result["detail"] = check.detail;
  }
  return result;
}

bool HasError(const std::vector<DoctorCheck> &checks)
{
  return std::any_of(
      checks.begin(),
      checks.end(),
      [](const DoctorCheck &check) {
        return check.status == "error";
      });
}

json ReadJsonFile(const fs::path &path)
{
  std::ifstream input(path);
  if (!input)
  {
    throw std::runtime_error("failed to read " + path.string());
  }
  json value;
  input >> value;
  return value;
}

void PrintHuman(const json &payload)
{
  std::cout << "pafio doctor\n";
  for (const json &check : payload.at("checks"))
  {
    std::cout << "  [" << check.at("status").get<std::string>() << "] "
              << check.at("name").get<std::string>() << ": "
              << check.at("message").get<std::string>() << '\n';
  }
}

}  // namespace

namespace pafio
{

int HandleDoctor(const std::vector<std::string> &args, const bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("doctor");
  }

  bool local_json = as_json;
  fs::path manifest_path = "pafio.toml";
  std::optional<std::string> styio_bin;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--json")
    {
      local_json = true;
    }
    else if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError(
            {"UsageError", kExitUsage, "--manifest-path requires a value",
             "doctor"},
            as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--styio-bin")
    {
      if (++index >= args.size())
      {
        return EmitError(
            {"UsageError", kExitUsage, "--styio-bin requires a value",
             "doctor"},
            as_json);
      }
      styio_bin = args[index];
    }
    else
    {
      return EmitError(
          {"UsageError", kExitUsage,
           "unexpected argument for doctor: " + args[index], "doctor"},
          as_json);
    }
  }

  std::vector<DoctorCheck> checks;
  checks.reserve(6);

  try
  {
    const ManifestDocument manifest = LoadManifest(manifest_path);
    checks.push_back({
        .name = "manifest",
        .status = "ok",
        .message = "manifest is valid",
        .detail = {
            {"has_package", manifest.package.has_value()},
            {"has_workspace", manifest.workspace.has_value()},
            {"path", CanonicalAbsolutePath(manifest_path).string()},
        },
    });
  }
  catch (const std::exception &err)
  {
    checks.push_back({
        .name = "manifest",
        .status = "error",
        .message = err.what(),
    });
  }

  const fs::path canonical_manifest = CanonicalAbsolutePath(manifest_path);
  const fs::path lockfile_path =
      canonical_manifest.parent_path() / "pafio.lock";
  if (!fs::is_regular_file(lockfile_path))
  {
    checks.push_back({
        .name = "lock",
        .status = "warning",
        .message = "lockfile is not present",
        .detail = {{"path", lockfile_path.string()}},
    });
  }
  else
  {
    try
    {
      const LockfileDocument lockfile = LoadLockfile(lockfile_path);
      checks.push_back({
          .name = "lock",
          .status = "ok",
          .message = "lockfile is valid",
          .detail = {
              {"packages", lockfile.packages.size()},
              {"path", lockfile_path.string()},
          },
      });
    }
    catch (const std::exception &err)
    {
      checks.push_back({
          .name = "lock",
          .status = "error",
          .message = err.what(),
      });
    }
  }

  const fs::path resolution_path =
      ProjectStateRootForManifest(canonical_manifest) / "resolution-v1.json";
  if (!fs::is_regular_file(resolution_path))
  {
    checks.push_back({
        .name = "resolution",
        .status = "warning",
        .message = "resolution state is not present",
        .detail = {{"path", resolution_path.string()}},
    });
  }
  else
  {
    try
    {
      const json resolution = ReadJsonFile(resolution_path);
      if (!resolution.is_object() ||
          resolution.value("schema_version", 0) != 1 ||
          !resolution.contains("packages") ||
          !resolution.at("packages").is_array())
      {
        throw std::runtime_error("resolution state is not resolution v1");
      }
      checks.push_back({
          .name = "resolution",
          .status = "ok",
          .message = "resolution state is valid",
          .detail = {
              {"packages", resolution.at("packages").size()},
              {"path", resolution_path.string()},
          },
      });
    }
    catch (const std::exception &err)
    {
      checks.push_back({
          .name = "resolution",
          .status = "error",
          .message = err.what(),
      });
    }
  }

  const std::optional<fs::path> home = ResolveOptionalPafioHome();
  if (!home.has_value())
  {
    checks.push_back({
        .name = "cache",
        .status = "warning",
        .message = "shared package cache location is not configured",
    });
  }
  else if (!fs::exists(*home))
  {
    checks.push_back({
        .name = "cache",
        .status = "warning",
        .message = "shared package cache has not been created",
        .detail = {{"path", home->string()}},
    });
  }
  else if (!fs::is_directory(*home))
  {
    checks.push_back({
        .name = "cache",
        .status = "error",
        .message = "shared package cache path is not a directory",
        .detail = {{"path", home->string()}},
    });
  }
  else
  {
    checks.push_back({
        .name = "cache",
        .status = "ok",
        .message = "shared package cache is readable",
        .detail = {{"path", home->string()}},
    });
  }

  if (!home.has_value())
  {
    checks.push_back({
        .name = "registry_trust",
        .status = "warning",
        .message = "registry trust store location is not configured",
    });
  }
  else
  {
    const fs::path trust_store = RegistryTrustStorePath(*home);
    if (!fs::is_regular_file(trust_store))
    {
      checks.push_back({
          .name = "registry_trust",
          .status = "warning",
          .message = "registry trust store is not present",
          .detail = {{"path", trust_store.string()}},
      });
    }
    else
    {
      try
      {
        const std::vector<RegistryTrustPin> pins =
            LoadRegistryTrustPins(*home);
        checks.push_back({
            .name = "registry_trust",
            .status = "ok",
            .message = "registry trust store is valid",
            .detail = {
                {"path", trust_store.string()},
                {"pins", pins.size()},
            },
        });
      }
      catch (const std::exception &err)
      {
        checks.push_back({
            .name = "registry_trust",
            .status = "error",
            .message = err.what(),
        });
      }
    }
  }

  try
  {
    const std::optional<fs::path> compiler = ResolveStyioBinary(styio_bin);
    if (!compiler.has_value())
    {
      checks.push_back({
          .name = "styio",
          .status = "error",
          .message =
              "Styio was not found through --styio-bin, PAFIO_STYIO_BIN, or "
              "PATH",
      });
    }
    else
    {
      const CompatibilityReport report =
          CheckCompilerCompatibility(*compiler);
      checks.push_back({
          .name = "styio",
          .status = "ok",
          .message = "external Styio machine contract is compatible",
          .detail = {
              {"binary", report.binary.string()},
              {"compiler_channel", report.compiler_channel},
              {"compiler_version", report.compiler_version},
              {"supported_compile_plan_versions",
               report.supported_compile_plan_versions},
          },
      });
    }
  }
  catch (const std::exception &err)
  {
    checks.push_back({
        .name = "styio",
        .status = "error",
        .message = err.what(),
    });
  }

  json payload = {
      {"checks", json::array()},
      {"command", "doctor"},
      {"ok", !HasError(checks)},
  };
  for (const DoctorCheck &check : checks)
  {
    payload["checks"].push_back(SerializeCheck(check));
  }

  if (local_json)
  {
    std::cout << payload.dump() << '\n';
  }
  else
  {
    PrintHuman(payload);
  }
  return payload.at("ok").get<bool>() ? kExitSuccess : kExitContract;
}

}  // namespace pafio
