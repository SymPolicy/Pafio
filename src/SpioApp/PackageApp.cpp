#include "SpioApp/PackageApp.hpp"

#include "SpioCLI/Support.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Process.hpp"
#include "SpioManifest/Lockfile.hpp"
#include "SpioManifest/Manifest.hpp"
#include "SpioPack/Pack.hpp"
#include "SpioPublish/Publish.hpp"
#include "SpioResolve/Resolver.hpp"
#include "SpioSecurity/RegistrySecurity.hpp"
#include "SpioTree/Render.hpp"
#include "SpioVendor/Vendor.hpp"
#include "SpioWorkflow/Dependencies.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

std::string NormalizeRegistryRootValue(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

bool EndsWith(std::string_view value, std::string_view suffix)
{
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

json ParseJsonObjectText(const std::string &text, const std::string &context)
{
  try
  {
    json payload = json::parse(text);
    if (!payload.is_object())
    {
      throw spio::PublishError(context + " must emit a top-level JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &)
  {
    throw spio::PublishError(context + " did not emit valid JSON");
  }
}

std::string ProcessFailureText(const spio::ProcessResult &result)
{
  return spio::DescribeProcessFailure(result);
}

fs::path RegistryV2KeyDirectory()
{
  const std::optional<fs::path> spio_home = spio::ResolveOptionalSpioHome();
  if (!spio_home.has_value())
  {
    throw spio::PublishError("unable to resolve SPIO_HOME for registry v2 signing keys: set SPIO_HOME or HOME");
  }
  return spio::RegistryServerRoot(*spio_home) / "v2" / "keys";
}

void EnsureRegistryV2KeyDirectory(const fs::path &key_dir)
{
  if (fs::exists(key_dir / "keys.json"))
  {
    return;
  }

  const spio::ProcessResult result = spio::RunProcess<spio::PublishError>({
      .program = "python3",
      .args = {
          (spio::ProjectRoot() / "scripts" / "registry-v2-keygen.py").string(),
          "--output-dir",
          key_dir.string(),
      },
      .search_path = true,
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry v2 key generation",
  });
  if (result.exit_code != 0)
  {
    throw spio::PublishError("failed to initialize registry v2 signing keys: " + ProcessFailureText(result));
  }
}

json RunRegistryV2FilesystemPublish(const fs::path &registry_root, const spio::PublishResult &candidate)
{
  const fs::path key_dir = RegistryV2KeyDirectory();
  EnsureRegistryV2KeyDirectory(key_dir);

  const spio::ProcessResult result = spio::RunProcess<spio::PublishError>({
      .program = "python3",
      .args = {
          (spio::ProjectRoot() / "scripts" / "registry-v2-publish.py").string(),
          "--root",
          registry_root.string(),
          "--key-dir",
          key_dir.string(),
          "--archive-path",
          candidate.archive_path.string(),
          "--publisher-id",
          "spio-cli",
      },
      .search_path = true,
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry v2 publish",
  });
  if (result.exit_code != 0)
  {
    throw spio::PublishError("registry v2 filesystem publish failed: " + ProcessFailureText(result));
  }

  return ParseJsonObjectText(result.stdout_text, "registry v2 publish");
}

std::string RegistryControlPlaneBaseUrl(std::string registry_root)
{
  constexpr std::string_view kControlPlaneBase = "/api/spio-registry-control/v1";
  registry_root = NormalizeRegistryRootValue(std::move(registry_root));
  if (EndsWith(registry_root, kControlPlaneBase))
  {
    return registry_root;
  }
  return registry_root + std::string(kControlPlaneBase);
}

json RunRegistryV2ControlPlanePublish(
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const spio::PublishResult &candidate)
{
  const std::string control_plane_base = RegistryControlPlaneBaseUrl(registry_root);
  json request_body = {
      {"archive_path", candidate.archive_path.string()},
      {"publisher_id", "spio-cli"},
  };

  std::vector<std::string> args = {
      "-fsS",
      "-X",
      "POST",
      control_plane_base + "/publish",
      "-H",
      "Content-Type: application/json",
  };
  for (const std::string &header : request_headers)
  {
    args.push_back("-H");
    args.push_back(header);
  }
  args.push_back("--data");
  args.push_back(request_body.dump());

  const spio::ProcessResult result = spio::RunProcess<spio::PublishError>({
      .program = "curl",
      .args = args,
      .search_path = true,
      .timeout = spio::kExternalProcessStepTimeout,
      .error_context = "registry control-plane publish request",
  });
  if (result.exit_code != 0)
  {
    throw spio::PublishError("registry control-plane publish request failed: " + ProcessFailureText(result));
  }

  const json envelope = ParseJsonObjectText(result.stdout_text, "registry control-plane publish response");
  const int returncode = envelope.value("returncode", -1);
  if (returncode != 0)
  {
    std::string detail;
    if (envelope.contains("error_payload") && envelope["error_payload"].is_object())
    {
      detail = envelope["error_payload"].value("detail", "");
    }
    if (detail.empty())
    {
      detail = envelope.value("stderr", "");
    }
    if (detail.empty())
    {
      detail = envelope.value("message", "registry control-plane returned a failure envelope");
    }
    throw spio::PublishError("registry control-plane publish failed: " + detail);
  }
  if (!envelope.contains("payload") || !envelope["payload"].is_object())
  {
    throw spio::PublishError("registry control-plane publish response is missing payload");
  }
  return envelope["payload"];
}

json BuildFilesystemPublishPayload(
    const spio::PublishResult &candidate,
    const fs::path &registry_root,
    const json &publish_result)
{
  return {
      {"command", "publish"},
      {"mode", "publish"},
      {"transport", "filesystem"},
      {"registry_protocol", "v2"},
      {"message", "published package into registry v2 static root: " + (registry_root / publish_result.at("index_path").get<std::string>()).string()},
      {"manifest_path", candidate.manifest_path.string()},
      {"package_root", candidate.package_root.string()},
      {"archive_path", candidate.archive_path.string()},
      {"package", candidate.package_name},
      {"version", candidate.package_version},
      {"dependencies", candidate.dependency_count},
      {"dev_dependencies", candidate.dev_dependency_count},
      {"registry_root", registry_root.string()},
      {"registry_config_path", (registry_root / "config.json").string()},
      {"registry_index_path", (registry_root / publish_result.at("index_path").get<std::string>()).string()},
      {"registry_artifact_path", (registry_root / publish_result.at("artifact_path").get<std::string>()).string()},
      {"registry_log_leaf_path", (registry_root / publish_result.at("log_leaf_path").get<std::string>()).string()},
      {"created_root", publish_result.value("created_root", false)},
      {"sequence", publish_result.value("sequence", 0)},
      {"checkpoint_version", publish_result.value("checkpoint_version", 0)},
      {"snapshot_version", publish_result.value("snapshot_version", 0)},
      {"timestamp_version", publish_result.value("timestamp_version", 0)},
      {"sha256", publish_result.at("archive_sha256")},
      {"size_bytes", publish_result.at("archive_size_bytes")},
      {"published_at", publish_result.at("published_at")},
  };
}

json BuildHttpPublishPayload(
    const spio::PublishResult &candidate,
    const std::string &registry_root,
    const spio::RegistryWriteSecurityDecision &security,
    const json &publish_result)
{
  json payload = {
      {"command", "publish"},
      {"mode", "publish"},
      {"transport", "http"},
      {"registry_protocol", "v2"},
      {"message", "published package through registry v2 control plane: " + RegistryControlPlaneBaseUrl(registry_root) + "/publish"},
      {"manifest_path", candidate.manifest_path.string()},
      {"package_root", candidate.package_root.string()},
      {"archive_path", candidate.archive_path.string()},
      {"package", candidate.package_name},
      {"version", candidate.package_version},
      {"dependencies", candidate.dependency_count},
      {"dev_dependencies", candidate.dev_dependency_count},
      {"registry_root", NormalizeRegistryRootValue(registry_root)},
      {"control_plane_base_url", RegistryControlPlaneBaseUrl(registry_root)},
      {"publish_endpoint", RegistryControlPlaneBaseUrl(registry_root) + "/publish"},
      {"registry_index_path", publish_result.at("index_path")},
      {"registry_artifact_path", publish_result.at("artifact_path")},
      {"registry_log_leaf_path", publish_result.at("log_leaf_path")},
      {"created_root", publish_result.value("created_root", false)},
      {"sequence", publish_result.value("sequence", 0)},
      {"checkpoint_version", publish_result.value("checkpoint_version", 0)},
      {"snapshot_version", publish_result.value("snapshot_version", 0)},
      {"timestamp_version", publish_result.value("timestamp_version", 0)},
      {"registry_security_provider", security.provider_name},
      {"registry_write_security_mode", security.mode},
      {"registry_header_count", security.request_headers.size()},
      {"sha256", publish_result.at("archive_sha256")},
      {"size_bytes", publish_result.at("archive_size_bytes")},
      {"published_at", publish_result.at("published_at")},
  };
  if (security.profile_name.has_value())
  {
    payload["registry_profile"] = *security.profile_name;
  }
  return payload;
}

void WriteCanonicalLockfile(const fs::path &lockfile_path, const std::string &rendered)
{
  std::ofstream out(lockfile_path);
  if (!out)
  {
    throw std::runtime_error("failed to open lockfile for write: " + lockfile_path.string());
  }
  out << rendered;
  if (!out.good())
  {
    throw std::runtime_error("failed to write lockfile: " + lockfile_path.string());
  }
}

}  // namespace

namespace spio
{

int HandleNew(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("new");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "new requires a package name", "new"}, as_json);
  }

  size_t index = 0;
  const std::string name = args[index++];
  fs::path directory;
  if (index < args.size() && !args[index].starts_with("--"))
  {
    directory = fs::path(args[index++]);
  }
  else
  {
    const auto slash = name.find('/');
    directory = slash == std::string::npos ? fs::path(name) : fs::path(name.substr(slash + 1));
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", kExitUsage, "unexpected arguments for new", "new"}, as_json);
  }

  try
  {
    InitializeProject({.package_name = name, .root = directory, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", kExitUsage, err.what(), "new"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "new"},
          {"message", "initialized project at " + directory.string()},
          {"root", fs::absolute(directory).string()},
      },
      as_json);
}

int HandleInit(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("init");
  }
  std::optional<std::string> name;
  size_t index = 0;
  while (index < args.size())
  {
    if (args[index] == "--name")
    {
      ++index;
      if (index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--name requires a value", "init"}, as_json);
      }
      name = args[index++];
      continue;
    }
    break;
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", kExitUsage, "unexpected arguments for init", "init"}, as_json);
  }

  const fs::path root = fs::current_path();
  const std::string package_name = name.value_or(InferLocalPackageName(root));
  try
  {
    InitializeProject({.package_name = package_name, .root = root, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", kExitUsage, err.what(), "init"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "init"},
          {"message", "initialized project in " + root.string()},
          {"root", fs::absolute(root).string()},
          {"package", package_name},
      },
      as_json);
}

int HandleAdd(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("add");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "add requires a package name", "add"}, as_json);
  }

  AddDependencyRequest request{
      .manifest_path = "spio.toml",
      .package_name = args.front(),
  };
  bool saw_source = false;
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "add"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--alias")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--alias requires a value", "add"}, as_json);
      }
      request.alias = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = DependencySection::kDevDependencies;
    }
    else if (args[index] == "--path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--path requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = false;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--git")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--git requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = true;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_registry = true;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--rev")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--rev requires a value", "add"}, as_json);
      }
      request.rev = args[index];
    }
    else if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--version requires a value", "add"}, as_json);
      }
      request.version = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for add: " + args[index], "add"}, as_json);
    }
  }

  if (!saw_source)
  {
    return EmitError(
        {"UsageError", kExitUsage, "add requires --path <path>, --git <source> --rev <rev>, or --registry <url> --version <x.y.z>", "add"},
        as_json);
  }

  try
  {
    const DependencyCommandResult result = AddDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "add"},
            {"message", "added dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", DependencySectionName(result.section)},
            {"packages", result.package_count},
            {"source_kind", request.use_git ? "git" : (request.use_registry ? "registry" : "path")},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "add"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "add"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "add"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "add"}, as_json);
  }
}

int HandleRemove(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("remove");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "remove requires a dependency alias or package name", "remove"}, as_json);
  }

  RemoveDependencyRequest request{
      .manifest_path = "spio.toml",
      .target = args.front(),
  };
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "remove"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = DependencySection::kDevDependencies;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for remove: " + args[index], "remove"}, as_json);
    }
  }

  try
  {
    const DependencyCommandResult result = RemoveDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "remove"},
            {"message", "removed dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", DependencySectionName(result.section)},
            {"packages", result.package_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "remove"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "remove"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "remove"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "remove"}, as_json);
  }
}

int HandleFetch(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("fetch");
  }

  fs::path manifest_path = "spio.toml";
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "fetch"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for fetch: " + args[index], "fetch"}, as_json);
    }
  }

  const ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "fetch", workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }

  try
  {
    const FetchCommandResult result = FetchDependencies(manifest_path, resolve_options);
    return EmitSuccess(
        {
            {"command", "fetch"},
            {"message", "fetched dependency sources for " + std::to_string(result.package_count) + " package(s)"},
            {"manifest_path", result.manifest_path.string()},
            {"packages", result.package_count},
            {"git_packages", result.git_package_count},
            {"registry_packages", result.registry_package_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "fetch"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "fetch"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "fetch"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "fetch"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "fetch"}, as_json);
  }
}

int HandleLock(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("lock");
  }

  fs::path manifest_path = "spio.toml";
  bool check_only = false;
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "lock"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (args[index] == "--check")
    {
      check_only = true;
    }
    else if (args[index] == "--offline")
    {
      workflow_flags.offline = true;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for lock: " + args[index], "lock"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", kExitManifest, "manifest not found: " + manifest_path.string(), "lock"}, as_json);
  }

  LockGenerationResult generated;
  const ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  try
  {
    generated = ResolveSingleVersionLockfile(manifest_path, resolve_options);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "lock"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "lock"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "lock"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "lock"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "lock"}, as_json);
  }

  const std::string rendered = SerializeLockfileCanonical(generated.lockfile);
  if (check_only)
  {
    if (!fs::exists(generated.lockfile_path))
    {
      return EmitError({"LockfileError", kExitLock, "lockfile missing: " + generated.lockfile_path.string(), "lock"}, as_json);
    }

    try
    {
      if (ReadFile(generated.lockfile_path) != rendered)
      {
        return EmitError({"LockfileError", kExitLock, "lockfile is stale: " + generated.lockfile_path.string(), "lock"}, as_json);
      }
    }
    catch (const std::exception &err)
    {
      return EmitError({"LockfileError", kExitLock, err.what(), "lock"}, as_json);
    }

    return EmitSuccess(
        {
            {"command", "lock"},
            {"message", "lockfile is up to date: " + generated.lockfile_path.string()},
            {"manifest_path", generated.manifest_path.string()},
            {"lockfile_path", generated.lockfile_path.string()},
            {"mode", "check"},
            {"packages", generated.lockfile.packages.size()},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }

  try
  {
    WriteCanonicalLockfile(generated.lockfile_path, rendered);
  }
  catch (const std::exception &err)
  {
    return EmitError({"LockfileError", kExitLock, err.what(), "lock"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "lock"},
          {"message", "wrote lockfile: " + generated.lockfile_path.string()},
          {"manifest_path", generated.manifest_path.string()},
          {"lockfile_path", generated.lockfile_path.string()},
          {"mode", "write"},
          {"packages", generated.lockfile.packages.size()},
          {"offline", workflow_flags.offline},
      },
      as_json);
}

int HandleSync(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("sync");
  }

  fs::path manifest_path = "spio.toml";
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "sync"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for sync: " + args[index], "sync"}, as_json);
    }
  }

  const ResolveOptions resolve_options = BuildResolveOptions(manifest_path, workflow_flags);
  if (const auto lock_policy_error = ValidateLockedPolicy(manifest_path, "sync", workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }

  try
  {
    const LockGenerationResult generated = ResolveSingleVersionLockfile(manifest_path, resolve_options);
    const std::string rendered = SerializeLockfileCanonical(generated.lockfile);
    std::string lockfile_mode = workflow_flags.locked ? "locked" : "unchanged";

    if (!workflow_flags.locked)
    {
      bool write_lockfile = true;
      if (fs::exists(generated.lockfile_path))
      {
        write_lockfile = ReadFile(generated.lockfile_path) != rendered;
      }
      if (write_lockfile)
      {
        WriteCanonicalLockfile(generated.lockfile_path, rendered);
        lockfile_mode = "write";
      }
    }

    const FetchCommandResult fetched = FetchDependencies(manifest_path, resolve_options);
    return EmitSuccess(
        {
            {"command", "sync"},
            {"message", "synced project dependencies for " + std::to_string(generated.lockfile.packages.size()) + " package(s)"},
            {"manifest_path", generated.manifest_path.string()},
            {"lockfile_path", generated.lockfile_path.string()},
            {"lockfile_mode", lockfile_mode},
            {"packages", generated.lockfile.packages.size()},
            {"git_packages", fetched.git_package_count},
            {"registry_packages", fetched.registry_package_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "sync"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "sync"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "sync"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "sync"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "sync"}, as_json);
  }
  catch (const std::exception &err)
  {
    return EmitError({"LockfileError", kExitLock, err.what(), "sync"}, as_json);
  }
}

int HandleTree(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("tree");
  }

  fs::path manifest_path = "spio.toml";
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "tree"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tree: " + args[index], "tree"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", kExitManifest, "manifest not found: " + manifest_path.string(), "tree"}, as_json);
  }

  LockGenerationResult graph;
  try
  {
    graph = ResolveSingleVersionLockfile(manifest_path);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "tree"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "tree"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "tree"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "tree"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "tree"}, as_json);
  }

  if (as_json)
  {
    json packages = json::array();
    for (const LockPackage &package : graph.lockfile.packages)
    {
      json item{
          {"id", package.id},
          {"name", package.name},
          {"version", package.version},
          {"source_kind", package.source_kind},
          {"dependencies", package.dependencies},
      };
      if (package.git.has_value())
      {
        item["git"] = *package.git;
      }
      if (package.rev.has_value())
      {
        item["rev"] = *package.rev;
      }
      if (package.registry.has_value())
      {
        item["registry"] = *package.registry;
      }
      if (package.sha256.has_value())
      {
        item["sha256"] = *package.sha256;
      }
      packages.push_back(std::move(item));
    }

    std::cout << json({
                     {"command", "tree"},
                     {"manifest_path", graph.manifest_path.string()},
                     {"lockfile_path", graph.lockfile_path.string()},
                     {"resolver", graph.lockfile.resolver},
                     {"root_ids", graph.root_ids},
                     {"packages", packages},
                 })
                     .dump()
              << '\n';
    return kExitSuccess;
  }

  std::cout << RenderDependencyTreeText(graph);
  return kExitSuccess;
}

int HandleVendor(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("vendor");
  }

  VendorRequest request;
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "vendor"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "vendor"}, as_json);
      }
      request.output_path = fs::path(args[index]);
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for vendor: " + args[index], "vendor"}, as_json);
    }
  }

  const ResolveOptions resolve_options = BuildResolveOptions(request.manifest_path, workflow_flags, request.output_path);
  if (const auto lock_policy_error = ValidateLockedPolicy(request.manifest_path, "vendor", workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }
  request.offline = workflow_flags.offline;

  try
  {
    const VendorResult result = WriteVendorTree(request);
    return EmitSuccess(
        {
            {"command", "vendor"},
            {"message", "materialized vendored dependency snapshots under " + result.vendor_root.string()},
            {"manifest_path", result.manifest_path.string()},
            {"vendor_root", result.vendor_root.string()},
            {"metadata_path", result.metadata_path.string()},
            {"packages", result.package_count},
            {"git_snapshots", result.git_snapshot_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "vendor"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "vendor"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "vendor"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "vendor"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "vendor"}, as_json);
  }
  catch (const VendorError &err)
  {
    return EmitError({"VendorError", kExitVendor, err.what(), "vendor"}, as_json);
  }
}

int HandlePack(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("pack");
  }

  PackRequest request;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "pack"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--package requires a value", "pack"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "pack"}, as_json);
      }
      request.output_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for pack: " + args[index], "pack"}, as_json);
    }
  }

  try
  {
    const PackResult result = WriteSourcePackage(request);
    return EmitSuccess(
        {
            {"command", "pack"},
            {"message", "wrote source package: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"archive_prefix", result.archive_prefix},
            {"package", result.package_name},
            {"version", result.package_version},
            {"files", result.file_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "pack"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "pack"}, as_json);
  }
  catch (const PackError &err)
  {
    return EmitError({"PackError", kExitPack, err.what(), "pack"}, as_json);
  }
}

int HandlePublish(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("publish");
  }

  PublishRequest request;
  std::optional<std::string> registry_root;
  std::optional<std::string> registry_profile;
  std::optional<fs::path> registry_policy_file;
  std::vector<std::string> registry_headers;
  bool dry_run = false;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "publish"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--package requires a value", "publish"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "publish"}, as_json);
      }
      request.output_path = args[index];
    }
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry requires a value", "publish"}, as_json);
      }
      registry_root = args[index];
    }
    else if (args[index] == "--registry-profile")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry-profile requires a value", "publish"}, as_json);
      }
      registry_profile = args[index];
    }
    else if (args[index] == "--registry-policy-file")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry-policy-file requires a value", "publish"}, as_json);
      }
      registry_policy_file = args[index];
    }
    else if (args[index] == "--registry-header")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry-header requires a value", "publish"}, as_json);
      }
      const std::optional<std::string> normalized = NormalizeRegistryHeader(args[index]);
      if (!normalized.has_value())
      {
        return EmitError(
            {"UsageError", kExitUsage, "--registry-header must match <name:value> with non-empty name and value", "publish"},
            as_json);
      }
      registry_headers.push_back(*normalized);
    }
    else if (args[index] == "--dry-run")
    {
      dry_run = true;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for publish: " + args[index], "publish"}, as_json);
    }
  }

  if (dry_run && !registry_headers.empty())
  {
    return EmitError(
        {"UsageError", kExitUsage, "--registry-header is not valid with --dry-run", "publish"},
        as_json);
  }
  if (dry_run && registry_policy_file.has_value())
  {
    return EmitError(
        {"UsageError", kExitUsage, "--registry-policy-file is not valid with --dry-run", "publish"},
        as_json);
  }
  if (dry_run && registry_profile.has_value())
  {
    return EmitError(
        {"UsageError", kExitUsage, "--registry-profile is not valid with --dry-run", "publish"},
        as_json);
  }
  if (registry_profile.has_value() && registry_policy_file.has_value())
  {
    return EmitError(
        {"UsageError", kExitUsage, "--registry-profile cannot be combined with --registry-policy-file", "publish"},
        as_json);
  }

  if (!dry_run)
  {
    if (!registry_root.has_value())
    {
      return EmitError(
          {"UsageError", kExitUsage, "publish requires --registry <path-or-url> unless --dry-run is set", "publish"},
          as_json);
    }

    try
    {
      const PublishResult candidate = PreparePublishCandidate(request);

      if (IsHttpRegistryRoot(*registry_root))
      {
        const RegistryWriteSecurityDecision security = ResolveRegistryWriteSecurity({
            .registry_root = *registry_root,
            .profile_name = registry_profile,
            .policy_file = registry_policy_file,
            .explicit_request_headers = registry_headers,
        });
        return EmitSuccess(
            BuildHttpPublishPayload(
                candidate,
                security.registry_root,
                security,
                RunRegistryV2ControlPlanePublish(security.registry_root, security.request_headers, candidate)),
            as_json);
      }

      const fs::path filesystem_registry_root =
          IsFileRegistryRoot(*registry_root) ? FileRegistryUrlToPath(*registry_root) : fs::path(*registry_root);
      if (registry_policy_file.has_value())
      {
        return EmitError(
            {
                "UsageError",
                kExitUsage,
                "--registry-policy-file is only valid for http:// or https:// registry roots",
                "publish",
            },
            as_json);
      }
      if (registry_profile.has_value())
      {
        return EmitError(
            {
                "UsageError",
                kExitUsage,
                "--registry-profile is only valid for http:// or https:// registry roots",
                "publish",
            },
            as_json);
      }
      if (!registry_headers.empty())
      {
        return EmitError(
            {"UsageError", kExitUsage, "--registry-header is only valid for http:// or https:// registry roots", "publish"},
            as_json);
      }
      return EmitSuccess(
          BuildFilesystemPublishPayload(
              candidate,
              fs::absolute(filesystem_registry_root).lexically_normal(),
              RunRegistryV2FilesystemPublish(filesystem_registry_root, candidate)),
          as_json);
    }
    catch (const ValidationError &err)
    {
      return EmitError({"ManifestError", kExitManifest, err.what(), "publish"}, as_json);
    }
    catch (const WorkspaceError &err)
    {
      return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "publish"}, as_json);
    }
    catch (const PackError &err)
    {
      return EmitError({"PackError", kExitPack, err.what(), "publish"}, as_json);
    }
    catch (const PublishError &err)
    {
      return EmitError({"PublishError", kExitPublish, err.what(), "publish"}, as_json);
    }
  }

  try
  {
    const PublishResult result = PreparePublishCandidate(request);
    return EmitSuccess(
        {
            {"command", "publish"},
            {"mode", "dry-run"},
            {"message", "prepared publish candidate: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"package", result.package_name},
            {"version", result.package_version},
            {"dependencies", result.dependency_count},
            {"dev_dependencies", result.dev_dependency_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "publish"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "publish"}, as_json);
  }
  catch (const PackError &err)
  {
    return EmitError({"PackError", kExitPack, err.what(), "publish"}, as_json);
  }
  catch (const PublishError &err)
  {
    return EmitError({"PublishError", kExitPublish, err.what(), "publish"}, as_json);
  }
}

}  // namespace spio
