#include "PafioCore/Process.hpp"

namespace pafio
{

std::string
DescribeProcessFailure(const ProcessResult &result) {
  if (result.timed_out) {
    return "process timed out";
  }
  if (!result.stderr_text.empty()) {
    return TrimTrailingNewline(result.stderr_text);
  }
  if (!result.stdout_text.empty()) {
    return TrimTrailingNewline(result.stdout_text);
  }
  if (result.terminated_by_signal) {
    return "process terminated by signal " + std::to_string(result.signal_number);
  }
  return "process exited with code " + std::to_string(result.exit_code);
}

std::string
TrimTrailingNewline(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

}  // namespace pafio
