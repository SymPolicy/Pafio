#include "SpioTree/Render.hpp"

#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

namespace
{

void RenderNode(
    const std::string &package_id,
    const std::unordered_map<std::string, const spio::LockPackage *> &packages_by_id,
    std::set<std::string> &path_stack,
    const std::string &prefix,
    bool is_root,
    bool is_last,
    std::ostringstream &out)
{
  const auto found = packages_by_id.find(package_id);
  if (found == packages_by_id.end())
  {
    if (!is_root)
    {
      out << prefix << (is_last ? "\\- " : "+- ");
    }
    out << package_id << " (missing)\n";
    return;
  }

  if (!is_root)
  {
    out << prefix << (is_last ? "\\- " : "+- ");
  }
  out << package_id << '\n';

  const auto [_, inserted] = path_stack.insert(package_id);
  if (!inserted)
  {
    return;
  }

  const spio::LockPackage &package = *found->second;
  const std::string child_prefix = is_root ? "" : prefix + (is_last ? "   " : "|  ");
  for (size_t index = 0; index < package.dependencies.size(); ++index)
  {
    const std::string &dependency_id = package.dependencies[index];
    const bool dependency_is_last = index + 1U == package.dependencies.size();
    if (path_stack.contains(dependency_id))
    {
      out << child_prefix << (dependency_is_last ? "\\- " : "+- ") << dependency_id << " (cycle)\n";
      continue;
    }
    RenderNode(dependency_id, packages_by_id, path_stack, child_prefix, false, dependency_is_last, out);
  }

  path_stack.erase(package_id);
}

}  // namespace

namespace spio
{

std::string RenderDependencyTreeText(const LockGenerationResult &graph)
{
  std::unordered_map<std::string, const LockPackage *> packages_by_id;
  packages_by_id.reserve(graph.lockfile.packages.size());
  for (const LockPackage &package : graph.lockfile.packages)
  {
    packages_by_id.emplace(package.id, &package);
  }

  std::ostringstream out;
  std::set<std::string> path_stack;
  for (size_t index = 0; index < graph.root_ids.size(); ++index)
  {
    if (index > 0)
    {
      out << '\n';
    }
    RenderNode(graph.root_ids[index], packages_by_id, path_stack, "", true, true, out);
  }
  return out.str();
}

}  // namespace spio
