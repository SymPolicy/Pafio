#include "PafioCore/Process.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

std::string DescribeProcessContext(std::string context)
{
  if (context.empty())
  {
    return "child process";
  }
  return context;
}

void CloseIfValid(int fd)
{
  if (fd >= 0)
  {
    close(fd);
  }
}

void SetNonBlocking(int fd, const std::string &context)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
  {
    throw pafio::ProcessFailure("failed to configure non-blocking pipe for " + context);
  }
}

void AppendOutputChunk(
    std::string &target,
    bool &truncated,
    std::string_view chunk,
    size_t limit)
{
  if (limit == 0)
  {
    truncated = truncated || !chunk.empty();
    return;
  }
  if (target.size() >= limit)
  {
    truncated = truncated || !chunk.empty();
    return;
  }
  const size_t remaining = limit - target.size();
  const size_t to_copy = std::min(remaining, chunk.size());
  target.append(chunk.data(), to_copy);
  if (to_copy < chunk.size())
  {
    truncated = true;
  }
}

char **CurrentEnvironment()
{
#if defined(__APPLE__)
  return *_NSGetEnviron();
#else
  extern char **environ;
  return environ;
#endif
}

void ClearChildEnvironment()
{
  std::vector<std::string> names;
  for (char **entry = CurrentEnvironment(); entry != nullptr && *entry != nullptr; ++entry)
  {
    const std::string_view item(*entry);
    const size_t separator = item.find('=');
    if (separator != std::string_view::npos)
    {
      names.emplace_back(item.data(), separator);
    }
  }
  for (const std::string &name : names)
  {
    unsetenv(name.c_str());
  }
}

void ApplyChildEnvironment(const pafio::ProcessRequest &request)
{
  if (request.clear_environment)
  {
    ClearChildEnvironment();
  }
  for (const auto &[name, value] : request.environment_overrides)
  {
    if (value.has_value())
    {
      setenv(name.c_str(), value->c_str(), 1);
    }
    else
    {
      unsetenv(name.c_str());
    }
  }
}

void WriteChildSetupError(const std::string &message)
{
  static_cast<void>(write(STDERR_FILENO, message.c_str(), message.size()));
}

void KillChildProcess(const pid_t child, const bool terminate_process_group)
{
  if (terminate_process_group)
  {
    kill(-child, SIGKILL);
  }
  else
  {
    kill(child, SIGKILL);
  }
}

}  // namespace

namespace pafio
{

ProcessResult RunProcessChecked(const ProcessRequest &request)
{
  const std::string context = DescribeProcessContext(request.error_context);

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (!request.stdin_text.empty() && pipe(stdin_pipe) != 0)
  {
    throw ProcessFailure("failed to create stdin pipe for " + context + " execution");
  }
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    CloseIfValid(stdin_pipe[0]);
    CloseIfValid(stdin_pipe[1]);
    CloseIfValid(stdout_pipe[0]);
    CloseIfValid(stdout_pipe[1]);
    throw ProcessFailure("failed to create pipes for " + context + " execution");
  }

  const pid_t child = fork();
  if (child < 0)
  {
    CloseIfValid(stdin_pipe[0]);
    CloseIfValid(stdin_pipe[1]);
    CloseIfValid(stdout_pipe[0]);
    CloseIfValid(stdout_pipe[1]);
    CloseIfValid(stderr_pipe[0]);
    CloseIfValid(stderr_pipe[1]);
    throw ProcessFailure("failed to fork " + context);
  }

  if (child == 0)
  {
    setpgid(0, 0);
    if (!request.stdin_text.empty())
    {
      dup2(stdin_pipe[0], STDIN_FILENO);
    }
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    CloseIfValid(stdin_pipe[0]);
    CloseIfValid(stdin_pipe[1]);
    CloseIfValid(stdout_pipe[0]);
    CloseIfValid(stdout_pipe[1]);
    CloseIfValid(stderr_pipe[0]);
    CloseIfValid(stderr_pipe[1]);

    ApplyChildEnvironment(request);
    if (request.working_directory.has_value() && chdir(request.working_directory->c_str()) != 0)
    {
      std::string message = "failed to chdir to '" + request.working_directory->string() + "': " + std::strerror(errno);
      WriteChildSetupError(message);
      _exit(127);
    }

    std::vector<char *> argv;
    argv.reserve(request.args.size() + 2U);
    argv.push_back(const_cast<char *>(request.program.c_str()));
    for (const std::string &arg : request.args)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    if (request.search_path)
    {
      execvp(request.program.c_str(), argv.data());
    }
    else
    {
      execv(request.program.c_str(), argv.data());
    }
    WriteChildSetupError("failed to execute program: " + request.program + ": " + std::strerror(errno));
    _exit(127);
  }

  CloseIfValid(stdin_pipe[0]);
  CloseIfValid(stdout_pipe[1]);
  CloseIfValid(stderr_pipe[1]);

  SetNonBlocking(stdout_pipe[0], context);
  SetNonBlocking(stderr_pipe[0], context);
  bool stdin_open = !request.stdin_text.empty();
  size_t stdin_written = 0;
  if (stdin_open)
  {
    SetNonBlocking(stdin_pipe[1], context);
  }

  ProcessResult result;
  const auto start = std::chrono::steady_clock::now();
  bool stdout_open = true;
  bool stderr_open = true;
  std::array<char, 4096> buffer{};

  while (stdout_open || stderr_open)
  {
    std::array<pollfd, 3> poll_fds{};
    nfds_t poll_count = 0;
    if (stdout_open)
    {
      poll_fds[poll_count++] = {.fd = stdout_pipe[0], .events = POLLIN | POLLHUP, .revents = 0};
    }
    if (stderr_open)
    {
      poll_fds[poll_count++] = {.fd = stderr_pipe[0], .events = POLLIN | POLLHUP, .revents = 0};
    }
    std::optional<nfds_t> stdin_index;
    if (stdin_open)
    {
      stdin_index = poll_count;
      poll_fds[poll_count++] = {.fd = stdin_pipe[1], .events = POLLOUT | POLLHUP | POLLERR, .revents = 0};
    }

    int timeout_ms = -1;
    if (request.timeout.has_value())
    {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed >= *request.timeout)
      {
        result.timed_out = true;
        KillChildProcess(child, request.terminate_process_group_on_timeout);
        break;
      }
      timeout_ms = static_cast<int>((*request.timeout - elapsed).count());
    }

    const int poll_result = poll(poll_fds.data(), poll_count, timeout_ms);
    if (poll_result < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      CloseIfValid(stdout_pipe[0]);
      CloseIfValid(stderr_pipe[0]);
      throw ProcessFailure("failed to poll process pipes for " + context);
    }

    if (poll_result == 0)
    {
      result.timed_out = true;
      KillChildProcess(child, request.terminate_process_group_on_timeout);
      break;
    }

    if (stdin_index.has_value() && (poll_fds[*stdin_index].revents & (POLLOUT | POLLHUP | POLLERR)))
    {
      while (stdin_written < request.stdin_text.size())
      {
        const ssize_t write_size = write(
            stdin_pipe[1],
            request.stdin_text.data() + stdin_written,
            request.stdin_text.size() - stdin_written);
        if (write_size > 0)
        {
          stdin_written += static_cast<size_t>(write_size);
          continue;
        }
        if (write_size < 0 && errno == EINTR)
        {
          continue;
        }
        if (write_size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
          break;
        }
        stdin_written = request.stdin_text.size();
        break;
      }
      if (stdin_written >= request.stdin_text.size())
      {
        CloseIfValid(stdin_pipe[1]);
        stdin_open = false;
      }
    }

    auto drain_fd = [&](int fd, std::string &text, bool &truncated, size_t limit, bool &is_open) {
      while (true)
      {
        const ssize_t read_size = read(fd, buffer.data(), buffer.size());
        if (read_size > 0)
        {
          AppendOutputChunk(text, truncated, std::string_view(buffer.data(), static_cast<size_t>(read_size)), limit);
          continue;
        }
        if (read_size == 0)
        {
          CloseIfValid(fd);
          is_open = false;
          return;
        }
        if (errno == EINTR)
        {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          return;
        }
        CloseIfValid(fd);
        is_open = false;
        return;
      }
    };

    nfds_t current = 0;
    if (stdout_open)
    {
      if (poll_fds[current].revents & (POLLIN | POLLHUP | POLLERR))
      {
        drain_fd(stdout_pipe[0], result.stdout_text, result.stdout_truncated, request.max_stdout_bytes, stdout_open);
      }
      ++current;
    }
    if (stderr_open)
    {
      if (poll_fds[current].revents & (POLLIN | POLLHUP | POLLERR))
      {
        drain_fd(stderr_pipe[0], result.stderr_text, result.stderr_truncated, request.max_stderr_bytes, stderr_open);
      }
    }
  }

  CloseIfValid(stdin_pipe[1]);
  CloseIfValid(stdout_pipe[0]);
  CloseIfValid(stderr_pipe[0]);

  int status = 0;
  if (waitpid(child, &status, 0) < 0)
  {
    throw ProcessFailure("failed to wait for " + context);
  }
  if (WIFEXITED(status))
  {
    result.exit_code = WEXITSTATUS(status);
  }
  else if (WIFSIGNALED(status))
  {
    result.terminated_by_signal = true;
    result.signal_number = WTERMSIG(status);
    result.exit_code = 128 + result.signal_number;
  }
  else
  {
    result.exit_code = 1;
  }
  return result;
}

std::string DescribeProcessFailure(const ProcessResult &result)
{
  if (result.timed_out)
  {
    return "process timed out";
  }
  if (!result.stderr_text.empty())
  {
    return TrimTrailingNewline(result.stderr_text);
  }
  if (!result.stdout_text.empty())
  {
    return TrimTrailingNewline(result.stdout_text);
  }
  if (result.terminated_by_signal)
  {
    return "process terminated by signal " + std::to_string(result.signal_number);
  }
  return "process exited with code " + std::to_string(result.exit_code);
}

std::string TrimTrailingNewline(std::string text)
{
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
  {
    text.pop_back();
  }
  return text;
}

}  // namespace pafio
