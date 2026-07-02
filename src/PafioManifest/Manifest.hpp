#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pafio
{

enum class DependencySourceKind
{
  kPath,
  kGit,
  kRegistry,
};

struct Dependency
{
  std::string alias;
  std::optional<std::string> package;
  DependencySourceKind source_kind = DependencySourceKind::kPath;
  std::string source;
  std::optional<std::string> rev;
  std::optional<std::string> version;
};

struct Toolchain
{
  std::string channel;
  bool implicit_std = true;
};

struct LibTarget
{
  std::string path;
};

struct BinTarget
{
  std::string name;
  std::string path;
};

struct TestTarget
{
  std::string name;
  std::string path;
};

struct WorkspaceConfig
{
  std::vector<std::string> members;
  std::vector<std::string> exclude;
  std::string resolver;
};

struct PackageConfig
{
  std::string name;
  std::string version;
  std::string edition;
  bool publish = false;
  Toolchain toolchain;
  std::optional<LibTarget> lib;
  std::vector<BinTarget> bins;
  std::vector<TestTarget> tests;
  std::vector<Dependency> dependencies;
  std::vector<Dependency> dev_dependencies;
};

struct ManifestDocument
{
  std::optional<PackageConfig> package;
  std::optional<WorkspaceConfig> workspace;
};

struct InitOptions
{
  std::string package_name;
  std::filesystem::path root;
  std::string kind;
};

ManifestDocument LoadManifest(const std::filesystem::path &manifest_path);
std::string SerializeManifestCanonical(const ManifestDocument &manifest);
std::string InferLocalPackageName(const std::filesystem::path &root);
void InitializeProject(const InitOptions &options);

}  // namespace pafio
