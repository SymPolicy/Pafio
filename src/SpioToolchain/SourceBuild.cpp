#include "SpioToolchain/SourceBuild.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Process.hpp"
#include "SpioToolchain/Vocabulary.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
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

std::string OfficialSourceOrigin()
{
  if (const char *explicit_origin = std::getenv("SPIO_STYIO_SOURCE_ORIGIN"); explicit_origin != nullptr && explicit_origin[0] != '\0')
  {
    return explicit_origin;
  }
  return "https://github.com/SymPolicy/styio.git";
}

std::string RequestedSourceRef(const spio::SourceBuildRequest &request)
{
  if (request.source_revision.has_value() && !request.source_revision->empty())
  {
    return *request.source_revision;
  }
  return request.channel;
}

bool ShouldFetchSource(const spio::SourceBuildRequest &request)
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
    throw spio::ToolError(
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
    throw spio::ToolError("styio source root not found: " + source_root.string());
  }
  if (!fs::is_directory(source_root))
  {
    throw spio::ToolError("styio source root must be a directory: " + source_root.string());
  }
  if (!fs::exists(source_root / "CMakeLists.txt"))
  {
    throw spio::ToolError("styio source root is missing CMakeLists.txt: " + source_root.string());
  }
}

std::string TryResolveGitRevision(const fs::path &source_root)
{
  if (!fs::exists(source_root / ".git"))
  {
    return {};
  }
  const spio::ProcessResult result = spio::RunProcess<spio::ToolError>({
      .program = "git",
      .args = {"rev-parse", "HEAD"},
      .working_directory = source_root,
      .timeout = spio::kExternalProcessProbeTimeout,
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
    const std::chrono::milliseconds timeout = spio::kExternalProcessStepTimeout)
{
  const spio::ProcessResult result = spio::RunProcess<spio::ToolError>({
      .program = program,
      .args = args,
      .working_directory = workdir,
      .timeout = timeout,
      .error_context = "source-toolchain process",
  });
  if (result.exit_code != 0)
  {
    const std::string detail = spio::DescribeProcessFailure(result);
    throw spio::ToolError(label + " failed" + (detail.empty() ? "" : ": " + detail));
  }
}

}  // namespace

namespace spio
{

SourceBuildResult EnsureSourceBuiltStyio(const SourceBuildRequest &request)
{
  if (!IsSupportedBuildMode(request.build_mode))
  {
    throw ToolError("unsupported source build mode: " + request.build_mode);
  }

  const fs::path spio_home = ResolveSpioHome();
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
  else if (const char *env_source_root = std::getenv("SPIO_STYIO_SOURCE_ROOT"); env_source_root != nullptr && env_source_root[0] != '\0')
  {
    source_root = CanonicalAbsolutePath(env_source_root);
    EnsureDirectorySourceRoot(source_root);
    source_revision = TryResolveGitRevision(source_root);
  }
  else
  {
    const std::string cache_identity = Slugify(OfficialSourceOrigin() + "#" + requested_ref);
    source_root = SourceCheckoutRoot(spio_home, request.channel, cache_identity);

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
  const fs::path build_root = SourceToolchainBuildRoot(spio_home, request.channel, build_identity, request.build_mode);
  const fs::path compiler_binary = CanonicalAbsolutePath(build_root / "bin" / "styio");

  bool built = false;
  if (!fs::exists(compiler_binary))
  {
    fs::create_directories(build_root);
    RunChecked(
        "cmake",
        {"-S", source_root.string(), "-B", build_root.string(), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"},
        std::nullopt,
        "cmake configure",
        spio::kExternalProcessBuildTimeout);
    RunChecked(
        "cmake",
        {"--build", build_root.string(), "--target", "styio"},
        std::nullopt,
        "cmake build",
        spio::kExternalProcessBuildTimeout);
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

}  // namespace spio
