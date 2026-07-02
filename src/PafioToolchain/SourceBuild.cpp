#include "PafioToolchain/SourceBuild.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioToolchain/Vocabulary.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace
{

uint64_t Fnv1a64(const std::string &text)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : text)
  {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string Hex64(const uint64_t value)
{
  std::ostringstream out;
  out << std::hex;
  out.width(16);
  out.fill('0');
  out << value;
  return out.str();
}

std::string Slugify(const std::string &text)
{
  std::string slug;
  slug.reserve(text.size() + 17U);
  for (const unsigned char ch : text)
  {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')
    {
      slug.push_back(static_cast<char>(ch));
    }
    else
    {
      slug.push_back('_');
    }
  }
  slug += "-";
  slug += Hex64(Fnv1a64(text));
  return slug;
}

std::string Trim(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
  {
    text.pop_back();
  }
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r'))
  {
    ++start;
  }
  return text.substr(start);
}

bool IsExecutableFile(const fs::path &path)
{
  std::error_code error;
  if (!fs::exists(path, error) || fs::is_directory(path, error))
  {
    return false;
  }
  return access(path.c_str(), X_OK) == 0;
}

bool ProgramExistsOnPath(std::string_view program)
{
  const char *path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0')
  {
    return false;
  }
  std::string_view path_list(path_env);
  while (!path_list.empty())
  {
    const size_t separator = path_list.find(':');
    const std::string_view directory = path_list.substr(0, separator);
    if (!directory.empty())
    {
      const fs::path candidate = fs::path(std::string(directory)) / std::string(program);
      if (IsExecutableFile(candidate))
      {
        return true;
      }
    }
    if (separator == std::string_view::npos)
    {
      break;
    }
    path_list.remove_prefix(separator + 1U);
  }
  return false;
}

std::optional<std::string> PreferredCmakeGenerator()
{
  if (const char *generator = std::getenv("PAFIO_CMAKE_GENERATOR"); generator != nullptr && generator[0] != '\0')
  {
    return std::string(generator);
  }
  if (ProgramExistsOnPath("ninja"))
  {
    return std::string("Ninja");
  }
  return std::nullopt;
}

std::vector<std::string> CmakeConfigureArgs(const fs::path &source_root, const fs::path &build_root)
{
  std::vector<std::string> args = {
      "-S",
      source_root.string(),
      "-B",
      build_root.string(),
      "-DCMAKE_BUILD_TYPE=Release",
  };
  if (const std::optional<std::string> generator = PreferredCmakeGenerator(); generator.has_value())
  {
    args.push_back("-G");
    args.push_back(*generator);
  }
  return args;
}

std::string OfficialSourceOrigin()
{
  if (const char *explicit_origin = std::getenv("PAFIO_STYIO_SOURCE_ORIGIN"); explicit_origin != nullptr && explicit_origin[0] != '\0')
  {
    return explicit_origin;
  }
  return "https://github.com/SymPolicy/styio.git";
}

std::string RequestedSourceRef(const pafio::SourceBuildRequest &request)
{
  if (request.source_revision.has_value() && !request.source_revision->empty())
  {
    return *request.source_revision;
  }
  return request.channel;
}

bool ShouldFetchSource(const pafio::SourceBuildRequest &request)
{
  if (!request.allow_fetch || request.offline)
  {
    return false;
  }
  if (request.assume_yes)
  {
    return true;
  }
  if (request.non_interactive || !isatty(STDIN_FILENO))
  {
    throw pafio::ToolError(
        "styio source tree not found locally; rerun with --yes, configure a local source root, or disable source-build mode");
  }

  std::cout << "styio source tree not found locally. Fetch from official Styio source origin? [Y/n] " << std::flush;
  std::string answer;
  if (!std::getline(std::cin, answer))
  {
    return false;
  }
  answer = Trim(answer);
  if (answer.empty() || answer == "Y" || answer == "y" || answer == "yes" || answer == "YES")
  {
    return true;
  }
  if (answer == "N" || answer == "n" || answer == "no" || answer == "NO")
  {
    return false;
  }
  return false;
}

void EnsureDirectorySourceRoot(const fs::path &source_root)
{
  if (!fs::exists(source_root))
  {
    throw pafio::ToolError("styio source root not found: " + source_root.string());
  }
  if (!fs::is_directory(source_root))
  {
    throw pafio::ToolError("styio source root must be a directory: " + source_root.string());
  }
  if (!fs::exists(source_root / "CMakeLists.txt"))
  {
    throw pafio::ToolError("styio source root is missing CMakeLists.txt: " + source_root.string());
  }
}

std::string TryResolveGitRevision(const fs::path &source_root)
{
  if (!fs::exists(source_root / ".git"))
  {
    return {};
  }
  const pafio::ProcessResult result = pafio::RunProcess<pafio::ToolError>({
      .program = "git",
      .args = {"rev-parse", "HEAD"},
      .working_directory = source_root,
      .timeout = pafio::kExternalProcessProbeTimeout,
      .error_context = "source-toolchain process",
  });
  if (result.exit_code != 0)
  {
    return {};
  }
  return Trim(result.stdout_text);
}

void RunChecked(
    const std::string &program,
    const std::vector<std::string> &args,
    const std::optional<fs::path> &workdir,
    const std::string &label,
    const std::chrono::milliseconds timeout = pafio::kExternalProcessStepTimeout)
{
  const pafio::ProcessResult result = pafio::RunProcess<pafio::ToolError>({
      .program = program,
      .args = args,
      .working_directory = workdir,
      .timeout = timeout,
      .error_context = "source-toolchain process",
  });
  if (result.exit_code != 0)
  {
    const std::string detail = pafio::DescribeProcessFailure(result);
    throw pafio::ToolError(label + " failed" + (detail.empty() ? "" : ": " + detail));
  }
}

}  // namespace

namespace pafio
{

SourceBuildResult EnsureSourceBuiltStyio(const SourceBuildRequest &request)
{
  if (!IsSupportedBuildMode(request.build_mode))
  {
    throw ToolError("unsupported source build mode: " + request.build_mode);
  }

  const fs::path pafio_home = ResolvePafioHome();
  const std::string requested_ref = RequestedSourceRef(request);

  fs::path source_root;
  std::string source_revision;
  bool fetched = false;
  if (request.explicit_source_root.has_value())
  {
    source_root = CanonicalAbsolutePath(*request.explicit_source_root);
    EnsureDirectorySourceRoot(source_root);
    source_revision = TryResolveGitRevision(source_root);
  }
  else if (const char *env_source_root = std::getenv("PAFIO_STYIO_SOURCE_ROOT"); env_source_root != nullptr && env_source_root[0] != '\0')
  {
    source_root = CanonicalAbsolutePath(env_source_root);
    EnsureDirectorySourceRoot(source_root);
    source_revision = TryResolveGitRevision(source_root);
  }
  else
  {
    const std::string cache_identity = Slugify(OfficialSourceOrigin() + "#" + requested_ref);
    source_root = SourceCheckoutRoot(pafio_home, request.channel, cache_identity);

    if (!fs::exists(source_root))
    {
      if (!ShouldFetchSource(request))
      {
        throw ToolError("styio source tree not available locally and fetch was declined");
      }
      fs::create_directories(source_root.parent_path());
      if (request.source_revision.has_value())
      {
        RunChecked("git", {"clone", "--no-checkout", OfficialSourceOrigin(), source_root.string()}, std::nullopt, "git clone");
        RunChecked("git", {"fetch", "--depth", "1", "origin", requested_ref}, source_root, "git fetch");
        RunChecked("git", {"checkout", "--force", "FETCH_HEAD"}, source_root, "git checkout");
      }
      else
      {
        RunChecked(
            "git",
            {"clone", "--depth", "1", "--branch", request.channel, OfficialSourceOrigin(), source_root.string()},
            std::nullopt,
            "git clone");
      }
      fetched = true;
    }
    else
    {
      EnsureDirectorySourceRoot(source_root);
      if (fs::exists(source_root / ".git") && !request.offline)
      {
        if (request.source_revision.has_value())
        {
          RunChecked("git", {"fetch", "--depth", "1", "origin", requested_ref}, source_root, "git fetch");
          RunChecked("git", {"checkout", "--force", "FETCH_HEAD"}, source_root, "git checkout");
        }
        else
        {
          RunChecked("git", {"fetch", "--depth", "1", "origin", request.channel}, source_root, "git fetch");
          RunChecked("git", {"checkout", "--force", "FETCH_HEAD"}, source_root, "git checkout");
        }
      }
    }

    source_revision = TryResolveGitRevision(source_root);
  }

  if (source_revision.empty())
  {
    source_revision = request.source_revision.value_or(Slugify(source_root.string()));
  }

  const std::string build_identity = Slugify(source_revision);
  const fs::path build_root = SourceToolchainBuildRoot(pafio_home, request.channel, build_identity, request.build_mode);
  const fs::path compiler_binary = CanonicalAbsolutePath(build_root / "bin" / "styio");

  bool built = false;
  if (!fs::exists(compiler_binary))
  {
    fs::create_directories(build_root);
    RunChecked(
        "cmake",
        CmakeConfigureArgs(source_root, build_root),
        std::nullopt,
        "cmake configure",
        pafio::kExternalProcessBuildTimeout);
    RunChecked(
        "cmake",
        {"--build", build_root.string(), "--target", "styio"},
        std::nullopt,
        "cmake build",
        pafio::kExternalProcessBuildTimeout);
    built = true;
  }

  if (!fs::exists(compiler_binary))
  {
    throw ToolError("source-built styio compiler binary was not produced at " + compiler_binary.string());
  }

  return SourceBuildResult{
      .source_root = source_root,
      .compiler_binary = compiler_binary,
      .source_revision = source_revision,
      .channel = request.channel,
      .build_mode = request.build_mode,
      .fetched = fetched,
      .built = built,
  };
}

}  // namespace pafio
