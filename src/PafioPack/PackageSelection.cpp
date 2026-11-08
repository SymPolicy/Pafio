#include "PafioPack/PackageSelection.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"

namespace pafio
{

std::vector<PackageSelection>
CollectPackageCandidates(const std::filesystem::path &root_manifest_path)
{
  const std::filesystem::path normalized_root_manifest = pafio::CanonicalAbsolutePath(root_manifest_path);
  const std::filesystem::path root_dir = normalized_root_manifest.parent_path();
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
      const std::filesystem::path member_manifest_path =
          pafio::CanonicalAbsolutePath(root_dir / member / "pafio.toml");
      const pafio::ManifestDocument member_manifest = pafio::LoadManifest(member_manifest_path);
      if (!member_manifest.package.has_value())
      {
        throw pafio::WorkspaceError(
            "workspace member manifest must define [package]: " + member_manifest_path.string());
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

}  // namespace pafio
