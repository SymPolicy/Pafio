#pragma once

#include <filesystem>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace spio
{

struct ProcessResult
{
  int exit_code = 0;
  bool terminated_by_signal = false;
  int signal_number = 0;
  bool timed_out = false;
  bool stdout_truncated = false;
  bool stderr_truncated = false;
  std::string stdout_text;
  std::string stderr_text;
};

struct ProcessRequest
{
  std::string program;
  std::vector<std::string> args;
  std::optional<std::filesystem::path> working_directory;
  bool search_path = true;
  std::map<std::string, std::optional<std::string>> environment_overrides;
  bool clear_environment = false;
  std::optional<std::chrono::milliseconds> timeout;
  size_t max_stdout_bytes = 1U << 20;
  size_t max_stderr_bytes = 1U << 20;
  std::string stdin_text;
  bool terminate_process_group_on_timeout = true;
  std::string error_context = "child process";
};

inline constexpr std::chrono::milliseconds kExternalProcessProbeTimeout{std::chrono::seconds{30}};
inline constexpr std::chrono::milliseconds kExternalProcessStepTimeout{std::chrono::minutes{10}};
inline constexpr std::chrono::milliseconds kExternalProcessBuildTimeout{std::chrono::minutes{60}};

class ProcessFailure : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

ProcessResult RunProcessChecked(const ProcessRequest &request);
std::string DescribeProcessFailure(const ProcessResult &result);
std::string TrimTrailingNewline(std::string text);

template <typename Error = ProcessFailure>
ProcessResult RunProcess(const ProcessRequest &request)
{
  try
  {
    return RunProcessChecked(request);
  }
  catch (const ProcessFailure &error)
  {
    throw Error(error.what());
  }
}

}  // namespace spio
