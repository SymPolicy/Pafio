#include "PafioCore/Process.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <rpc.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace
{

class Handle
{
public:
  explicit Handle(HANDLE value = nullptr) : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) {
      reset(std::exchange(other.value_, nullptr));
    }
    return *this;
  }
  HANDLE get() const { return value_; }
  explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
  void reset(HANDLE value = nullptr) {
    if (*this) CloseHandle(value_);
    value_ = value;
  }

private:
  HANDLE value_;
};

[[noreturn]] void Fail(const std::string &operation, DWORD error = GetLastError()) {
  throw pafio::ProcessFailure(operation + " (Windows error " + std::to_string(error) + ")");
}

std::wstring Wide(std::string_view value) {
  if (value.find('\0') != std::string_view::npos || value.size() > INT_MAX) {
    throw pafio::ProcessFailure("invalid process string");
  }
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
  if (!size) Fail("invalid UTF-8 process string");
  std::wstring result(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), size)) {
    Fail("failed to convert process string");
  }
  return result;
}

// The child CRT parses quotes and runs of backslashes; no command shell is used.
std::wstring Quote(std::wstring_view value) {
  std::wstring result(1, L'"');
  size_t slashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++slashes;
      continue;
    }
    result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (character == L'"') result.push_back(L'\\');
    result.push_back(character);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

struct EnvironmentLess
{
  bool operator()(const std::wstring &left, const std::wstring &right) const {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
  }
};
using Environment = std::map<std::wstring, std::wstring, EnvironmentLess>;

Environment ChildEnvironment(const pafio::ProcessRequest &request) {
  Environment values;
  if (!request.clear_environment) {
    wchar_t *block = GetEnvironmentStringsW();
    if (!block) Fail("failed to read process environment");
    try {
      for (const wchar_t *cursor = block; *cursor;) {
        const std::wstring entry(cursor);
        const size_t separator = entry.find(L'=', entry[0] == L'=' ? 1 : 0);
        if (separator != std::wstring::npos) {
          values.emplace(entry.substr(0, separator), entry.substr(separator + 1));
        }
        cursor += entry.size() + 1;
      }
    } catch (...) {
      FreeEnvironmentStringsW(block);
      throw;
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto &[name, value] : request.environment_overrides) {
    if (name.empty() || name.find('=') != std::string::npos) {
      throw pafio::ProcessFailure("invalid process environment name");
    }
    const std::wstring key = Wide(name);
    if (value) values[key] = Wide(*value);
    else values.erase(key);
  }
  return values;
}

std::vector<wchar_t> EnvironmentBlock(const Environment &values) {
  std::vector<wchar_t> block;
  for (const auto &[name, value] : values) {
    block.insert(block.end(), name.begin(), name.end());
    block.push_back(L'=');
    block.insert(block.end(), value.begin(), value.end());
    block.push_back(L'\0');
  }
  if (block.empty()) block.push_back(L'\0');
  block.push_back(L'\0');
  return block;
}

std::wstring Executable(const pafio::ProcessRequest &request, const Environment &environment,
                        const std::filesystem::path &directory) {
  const std::filesystem::path program(Wide(request.program));
  if (program.empty()) throw pafio::ProcessFailure("process program is empty");
  const auto resolve = [&](std::filesystem::path path) -> std::wstring {
    if (path.is_relative()) path = directory / path;
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error)) return path.wstring();
    if (!path.has_extension()) {
      path += L".exe";
      if (std::filesystem::is_regular_file(path, error)) return path.wstring();
    }
    return {};
  };
  if (!request.search_path || program.has_parent_path()) {
    const std::wstring resolved = resolve(program);
    if (!resolved.empty()) return resolved;
  } else if (const auto path = environment.find(L"PATH"); path != environment.end()) {
    size_t begin = 0;
    do {
      const size_t end = path->second.find(L';', begin);
      std::wstring entry = path->second.substr(begin, end - begin);
      if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') {
        entry = entry.substr(1, entry.size() - 2);
      }
      const std::wstring resolved = resolve(std::filesystem::path(entry) / program);
      if (!resolved.empty()) return resolved;
      if (end == std::wstring::npos) break;
      begin = end + 1;
    } while (true);
  }
  throw pafio::ProcessFailure("failed to resolve executable for " + request.error_context);
}

std::wstring PipeName() {
  UUID identifier;
  const RPC_STATUS created = UuidCreate(&identifier);
  if (created != RPC_S_OK && created != RPC_S_UUID_LOCAL_ONLY) {
    Fail("failed to create process pipe identity", created);
  }
  RPC_WSTR value = nullptr;
  const RPC_STATUS converted = UuidToStringW(&identifier, &value);
  if (converted != RPC_S_OK) Fail("failed to name process pipe", converted);
  std::wstring result;
  try {
    result = L"\\\\.\\pipe\\pafio-" + std::wstring(reinterpret_cast<wchar_t *>(value));
  } catch (...) {
    RpcStringFreeW(&value);
    throw;
  }
  RpcStringFreeW(&value);
  return result;
}

// One pending operation per stream keeps memory bounded and avoids reader threads.
class Pipe
{
public:
  explicit Pipe(bool input) : input_(input) {
    const std::wstring name = PipeName();
    server.reset(CreateNamedPipeW(name.c_str(), FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED |
                                 (input ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND),
                                 PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 1, 4096, 4096, 0, nullptr));
    if (!server) Fail("failed to create process pipe");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    child.reset(CreateFileW(name.c_str(), input ? GENERIC_READ : GENERIC_WRITE, 0,
                            &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!child) Fail("failed to open child process pipe");
    event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) Fail("failed to create process pipe event");
    operation_.hEvent = event.get();
    if (!ConnectNamedPipe(server.get(), &operation_)) {
      const DWORD error = GetLastError();
      if (error != ERROR_PIPE_CONNECTED) Fail("failed to connect process pipe", error);
    }
  }
  ~Pipe() { stop(); }

  void stop() noexcept {
    if (pending_) {
      CancelIoEx(server.get(), &operation_);
      DWORD transferred = 0;
      GetOverlappedResult(server.get(), &operation_, &transferred, TRUE);
      pending_ = false;
    }
    active = false;
    server.reset();
  }

  // Returns true for an immediately completed transfer, so the caller can pump
  // other streams and check the requested deadline before starting another one.
  bool pump(const pafio::ProcessRequest &request, std::string &output, bool &truncated, size_t limit) {
    if (!active || pending_) return false;
    if (input_ && offset_ == request.stdin_text.size()) {
      stop();
      return false;
    }
    ResetEvent(event.get());
    DWORD transferred = 0;
    const BOOL completed = input_
      ? WriteFile(server.get(), request.stdin_text.data() + offset_,
                  static_cast<DWORD>(std::min(size_t{4096}, request.stdin_text.size() - offset_)),
                  &transferred, &operation_)
      : ReadFile(server.get(), buffer_.data(), static_cast<DWORD>(buffer_.size()),
                 &transferred, &operation_);
    if (!completed) {
      const DWORD error = GetLastError();
      if (error == ERROR_IO_PENDING) pending_ = true;
      else finishError(error);
      return false;
    }
    consume(transferred, output, truncated, limit);
    return true;
  }

  void complete(std::string &output, bool &truncated, size_t limit) {
    DWORD transferred = 0;
    const BOOL completed = GetOverlappedResult(server.get(), &operation_, &transferred, FALSE);
    pending_ = false;
    if (!completed) finishError(GetLastError());
    else consume(transferred, output, truncated, limit);
  }

  Handle server;
  Handle child;
  Handle event;
  bool active = true;
  bool pending() const { return pending_; }

private:
  void finishError(DWORD error) {
    if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA) Fail("process pipe I/O failed", error);
    stop();
  }
  void consume(DWORD transferred, std::string &output, bool &truncated, size_t limit) {
    if (!transferred) {
      stop();
    } else if (input_) {
      offset_ += transferred;
    } else {
      const size_t retained = std::min(size_t{transferred}, limit - output.size());
      output.append(buffer_.data(), retained);
      truncated = truncated || retained < transferred;
    }
  }
  OVERLAPPED operation_{};
  std::array<char, 4096> buffer_{};
  size_t offset_ = 0;
  bool input_;
  bool pending_ = false;
};

class HandleList
{
public:
  explicit HandleList(std::array<HANDLE, 3> &handles) {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    storage_.resize(size);
    list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) Fail("failed to initialize process handle list");
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   handles.data(), sizeof(handles), nullptr, nullptr)) {
      const DWORD error = GetLastError();
      DeleteProcThreadAttributeList(list);
      Fail("failed to restrict inherited process handles", error);
    }
  }
  ~HandleList() { DeleteProcThreadAttributeList(list); }
  LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
private:
  std::vector<unsigned char> storage_;
};

struct ChildProcess
{
  Handle process;
  Handle thread;
  Handle job;
  bool finished = false;
  void terminate() {
    const BOOL terminated = job ? TerminateJobObject(job.get(), 1) : TerminateProcess(process.get(), 1);
    if (!terminated && WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) {
      Fail("failed to terminate child process");
    }
  }
  ~ChildProcess() {
    if (process && !finished) {
      if (job) TerminateJobObject(job.get(), 1);
      else TerminateProcess(process.get(), 1);
      WaitForSingleObject(process.get(), INFINITE);
    }
  }
};

}  // namespace

namespace pafio
{

ProcessResult RunProcessChecked(const ProcessRequest &request) {
  const Environment environment = ChildEnvironment(request);
  std::vector<wchar_t> environment_block = EnvironmentBlock(environment);
  const auto directory = std::filesystem::absolute(request.working_directory.value_or(std::filesystem::current_path()));
  const std::wstring executable = Executable(request, environment, directory);
  std::wstring command = Quote(executable);
  for (const std::string &argument : request.args) command += L" " + Quote(Wide(argument));

  Pipe output(false);
  Pipe error(false);
  std::unique_ptr<Pipe> input;
  Handle inherited_input;
  if (!request.stdin_text.empty()) {
    input = std::make_unique<Pipe>(true);
  } else {
    const HANDLE standard_input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE duplicate = nullptr;
    if (standard_input && standard_input != INVALID_HANDLE_VALUE) {
      if (!DuplicateHandle(GetCurrentProcess(), standard_input, GetCurrentProcess(),
                           &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        Fail("failed to inherit process stdin");
      }
      inherited_input.reset(duplicate);
    } else {
      SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
      inherited_input.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (!inherited_input) Fail("failed to open process stdin");
    }
  }
  std::array<HANDLE, 3> inherited{input ? input->child.get() : inherited_input.get(), output.child.get(), error.child.get()};
  HandleList attributes(inherited);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = inherited[0];
  startup.StartupInfo.hStdOutput = inherited[1];
  startup.StartupInfo.hStdError = inherited[2];
  startup.lpAttributeList = attributes.list;
  ChildProcess child;
  if (request.terminate_process_group_on_timeout) {
    child.job.reset(CreateJobObjectW(nullptr, nullptr));
    if (!child.job) Fail("failed to create process group");
  }
  PROCESS_INFORMATION information{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                      EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                      environment_block.data(), directory.c_str(), &startup.StartupInfo, &information)) {
    Fail("failed to start " + request.error_context);
  }
  child.process.reset(information.hProcess);
  child.thread.reset(information.hThread);
  if (child.job && !AssignProcessToJobObject(child.job.get(), child.process.get())) {
    // The suspended child has not entered the job; cleanup must target it directly.
    const DWORD error_code = GetLastError();
    child.job.reset();
    Fail("failed to assign child process group", error_code);
  }
  output.child.reset();
  error.child.reset();
  if (input) input->child.reset();
  inherited_input.reset();
  if (ResumeThread(child.thread.get()) == static_cast<DWORD>(-1)) Fail("failed to resume child process");
  child.thread.reset();

  ProcessResult result;
  const auto started = std::chrono::steady_clock::now();
  bool exited = false;
  std::string unused_output;
  bool unused_truncated = false;
  while (!exited || output.active || error.active || (input && input->active)) {
    DWORD remaining = INFINITE;
    if (request.timeout) {
      const auto elapsed = std::chrono::steady_clock::now() - started;
      if (elapsed >= *request.timeout) {
        result.timed_out = true;
        child.terminate();
        break;
      }
      const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(*request.timeout - elapsed).count();
      remaining = static_cast<DWORD>(std::min<int64_t>(milliseconds, INFINITE - 1));
    }
    bool ready = output.pump(request, result.stdout_text, result.stdout_truncated, request.max_stdout_bytes);
    ready = error.pump(request, result.stderr_text, result.stderr_truncated, request.max_stderr_bytes) || ready;
    if (input) ready = input->pump(request, unused_output, unused_truncated, 0) || ready;
    if (ready) continue;

    std::array<HANDLE, 4> handles{};
    std::array<Pipe *, 4> streams{};
    DWORD count = 0;
    if (!exited) handles[count++] = child.process.get();
    for (Pipe *pipe : {&output, &error, input.get()}) {
      if (pipe && pipe->pending()) {
        handles[count] = pipe->event.get();
        streams[count++] = pipe;
      }
    }
    if (!count) break;
    const DWORD status = WaitForMultipleObjects(count, handles.data(), FALSE, remaining);
    if (status == WAIT_TIMEOUT) continue;
    if (status >= WAIT_OBJECT_0 + count) Fail("failed to wait for process I/O");
    Pipe *pipe = streams[status - WAIT_OBJECT_0];
    if (!pipe) exited = true;
    else if (pipe == &output) pipe->complete(result.stdout_text, result.stdout_truncated, request.max_stdout_bytes);
    else if (pipe == &error) pipe->complete(result.stderr_text, result.stderr_truncated, request.max_stderr_bytes);
    else pipe->complete(unused_output, unused_truncated, 0);
  }
  output.stop();
  error.stop();
  if (input) input->stop();
  if (WaitForSingleObject(child.process.get(), INFINITE) != WAIT_OBJECT_0) Fail("failed to wait for child process");
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(child.process.get(), &exit_code)) Fail("failed to read child process exit status");
  result.exit_code = static_cast<int>(exit_code);
  child.finished = true;
  return result;
}

}  // namespace pafio
