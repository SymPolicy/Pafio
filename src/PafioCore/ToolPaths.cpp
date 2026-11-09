#include "PafioCore/ToolPaths.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Process.hpp"

#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{

std::mutex g_tool_paths_mutex;

bool IsExecutableFile(const std::filesystem::path &path)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec)
  {
    return false;
  }
#if defined(_WIN32)
  return true;
#else
  return ::access(path.c_str(), X_OK) == 0;
#endif
}

std::string ResolveOnPath(const std::string &name)
{
  const char *path_env = std::getenv("PATH");
  if (path_env == nullptr || *path_env == '\0')
  {
    throw pafio::FetchError("PATH is empty; cannot resolve tool '" + name + "'");
  }
  std::stringstream stream(path_env);
  std::string entry;
#if defined(_WIN32)
  const char delimiter = ';';
#else
  const char delimiter = ':';
#endif
  while (std::getline(stream, entry, delimiter))
  {
    if (entry.empty())
    {
      continue;
    }
    const std::filesystem::path candidate = std::filesystem::path(entry) / name;
    if (IsExecutableFile(candidate))
    {
      return pafio::ProcessPathString(std::filesystem::weakly_canonical(candidate));
    }
#if defined(_WIN32)
    const std::filesystem::path candidate_exe = std::filesystem::path(entry) / (name + ".exe");
    if (IsExecutableFile(candidate_exe))
    {
      return pafio::ProcessPathString(std::filesystem::weakly_canonical(candidate_exe));
    }
#endif
  }
  throw pafio::FetchError("failed to resolve absolute path for tool '" + name + "' on PATH");
}

const std::string &ResolveTool(const char *env_name, const char *default_name, std::string &slot)
{
  std::lock_guard<std::mutex> lock(g_tool_paths_mutex);
  if (!slot.empty())
  {
    return slot;
  }
  if (const char *override_path = std::getenv(env_name); override_path != nullptr && *override_path != '\0')
  {
    const std::filesystem::path path(override_path);
    if (!std::filesystem::path(override_path).is_absolute())
    {
      throw pafio::FetchError(std::string(env_name) + " must be an absolute path");
    }
    if (!IsExecutableFile(path))
    {
      throw pafio::FetchError(std::string(env_name) + " does not point to an executable: " + override_path);
    }
    slot = pafio::ProcessPathString(std::filesystem::weakly_canonical(path));
    return slot;
  }
  slot = ResolveOnPath(default_name);
  return slot;
}

std::string g_curl;
std::string g_git;
std::string g_tar;
std::string g_openssl;

}  // namespace

namespace pafio
{

const std::string &ResolvedCurlPath()
{
  return ResolveTool("PAFIO_CURL", "curl", g_curl);
}

const std::string &ResolvedGitPath()
{
  return ResolveTool("PAFIO_GIT", "git", g_git);
}

const std::string &ResolvedTarPath()
{
  return ResolveTool("PAFIO_TAR", "tar", g_tar);
}

const std::string &ResolvedOpenSslPath()
{
  return ResolveTool("PAFIO_OPENSSL", "openssl", g_openssl);
}

}  // namespace pafio
