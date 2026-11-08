#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "PafioCore/Process.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{

std::string executable;
constexpr std::string_view kCompilerProbeMachineInfo =
    R"json({"tool":"styio","compiler_version":"0.0.5","channel":"stable","active_integration_phase":"compile-plan-live","supported_contract_versions":{"machine_info":[1],"jsonl_diagnostics":[1],"compile_plan":[1],"runtime_events":[]},"supported_adapter_modes":["cli"],"feature_flags":{"single_file_entry":true,"jsonl_diagnostics":true,"compile_plan_consumer":true,"project_execution_via_compile_plan":true,"runtime_event_stream":false},"supported_contracts":{"compile_plan":[1]},"capabilities":["machine_info_json","single_file_entry","jsonl_diagnostics"],"edition_max":"2026"})json";

fs::path PathFromUtf8(const std::string &value) {
  return fs::path(std::u8string(value.begin(), value.end()));
}

#if defined(_WIN32)
std::string Utf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), count, nullptr, nullptr);
  return result;
}
#endif

std::string EnvironmentValue(const std::string &name) {
#if defined(_WIN32)
  const std::wstring key(name.begin(), name.end());
  const DWORD count = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
  if (!count) return "<unset>";
  std::wstring value(count, L'\0');
  value.resize(GetEnvironmentVariableW(key.c_str(), value.data(), count));
  return Utf8(value);
#else
  const char *value = std::getenv(name.c_str());
  return value ? value : "<unset>";
#endif
}

std::string EncodeArguments(const std::vector<std::string> &values) {
  std::string result;
  for (const std::string &value : values) result += std::to_string(value.size()) + ":" + value + "\n";
  return result;
}

struct TemporaryDirectory
{
  fs::path path;
  TemporaryDirectory() {
    static unsigned sequence = 0;
#if defined(_WIN32)
    const auto process_id = GetCurrentProcessId();
#else
    const auto process_id = getpid();
#endif
    path = fs::temp_directory_path() / ("pafio-process-" + std::to_string(process_id) + "-" + std::to_string(++sequence));
    fs::create_directory(path);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

pafio::ProcessRequest Request(const std::string &mode) {
  return {.program = executable, .args = {"--process-fixture", mode}, .search_path = false,
          .timeout = 5s, .error_context = "portable process test"};
}

int Fixture(const std::vector<std::string> &args) {
  const std::string &mode = args.at(2);
  if (mode == "arguments") {
    std::cout << EncodeArguments({args.begin() + 3, args.end()});
  } else if (mode == "environment") {
    for (size_t i = 3; i < args.size(); ++i) std::cout << EnvironmentValue(args[i]) << '\n';
  } else if (mode == "directory") {
    std::cout << pafio::ProcessPathString(fs::current_path());
  } else if (mode == "streams") {
    for (int i = 0; i < 64; ++i) {
      std::cout << std::string(4096, 'o') << std::flush;
      std::cerr << std::string(4096, 'e') << std::flush;
    }
    const std::string input(std::istreambuf_iterator<char>(std::cin), {});
    std::cout << "\ninput=" << input.size();
  } else if (mode == "busy") {
    for (;;) {
      std::cout << std::string(4096, 'b') << std::flush;
      std::this_thread::sleep_for(1ms);
    }
  } else if (mode == "pause") {
    std::this_thread::sleep_for(250ms);
  } else if (mode == "exit") {
    return std::stoi(args.at(3));
#if defined(_WIN32)
  } else if (mode == "inherited-handle") {
    DWORD flags = 0;
    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::stoull(args.at(3))));
    std::cout << (GetHandleInformation(handle, &flags) ? "inherited" : "closed");
  } else if (mode == "spawn-descendant") {
    std::wstring binary(32768, L'\0');
    binary.resize(GetModuleFileNameW(nullptr, binary.data(), static_cast<DWORD>(binary.size())));
    std::wstring command = L"\"" + binary + L"\" --process-fixture busy";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) return 90;
    std::cout << "pid=" << process.dwProcessId << '\n' << std::flush;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    for (;;) Sleep(INFINITE);
#endif
  } else {
    return 91;
  }
  return 0;
}

TEST(PortableProcess, PreservesLiteralArgumentsAndUnicode) {
  const std::vector<std::string> values{"", "two words", "\"quoted\"", "trailing\\", "quote\\\"slash",
                                         "a&b|c>d%PATH%", "line\nbreak", "\xe4\xb8\xad\xe6\x96\x87"};
  auto request = Request("arguments");
  request.args.insert(request.args.end(), values.begin(), values.end());
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, EncodeArguments(values));
  EXPECT_TRUE(result.stderr_text.empty());
}

TEST(PortableProcess, AppliesEnvironmentOverridesWithoutChangingParent) {
  auto request = Request("environment");
  request.args.insert(request.args.end(), {"PAFIO_PROCESS_VALUE", "PAFIO_PROCESS_REMOVED"});
  request.environment_overrides = {{"PAFIO_PROCESS_VALUE", "new \xe4\xb8\xad"}, {"PAFIO_PROCESS_REMOVED", std::nullopt}};
  const std::string before = EnvironmentValue("PAFIO_PROCESS_VALUE");
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, "new \xe4\xb8\xad\n<unset>\n");
  EXPECT_EQ(EnvironmentValue("PAFIO_PROCESS_VALUE"), before);
}

TEST(PortableProcess, CanClearInheritedEnvironment) {
  auto request = Request("environment");
  request.args.insert(request.args.end(), {"PATH", "PAFIO_PROCESS_VALUE"});
  request.clear_environment = true;
  request.environment_overrides = {{"PAFIO_PROCESS_VALUE", "only-explicit"}};
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, "<unset>\nonly-explicit\n");
}

TEST(PortableProcess, UsesRequestedWorkingDirectory) {
  TemporaryDirectory temporary;
  auto request = Request("directory");
  request.working_directory = fs::canonical(temporary.path);
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text == pafio::ProcessPathString(*request.working_directory));
}

TEST(PortableProcess, SearchesTheChildPathIncludingOverrides) {
  TemporaryDirectory temporary;
  const std::string filename =
#if defined(_WIN32)
    "process-fixture.exe";
#else
    "process-fixture";
#endif
  fs::copy_file(PathFromUtf8(executable), temporary.path / filename);
  auto request = Request("exit");
  request.program = "process-fixture";
  request.search_path = true;
  request.args.push_back("37");
  request.environment_overrides["PATH"] = pafio::ProcessPathString(fs::canonical(temporary.path));
  EXPECT_EQ(pafio::RunProcessChecked(request).exit_code, 37);
}

TEST(PortableProcess, DrainsBothOutputsWhileStreamingLargeInput) {
  auto request = Request("streams");
  request.stdin_text.assign(1U << 18, 'i');
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
  EXPECT_EQ(result.stdout_text, std::string(1U << 18, 'o') + "\ninput=262144");
  EXPECT_EQ(result.stderr_text, std::string(1U << 18, 'e'));
}

TEST(PortableProcess, BoundsCapturedBytesWithoutBlockingTheChild) {
  auto request = Request("streams");
  request.stdin_text = "input";
  request.max_stdout_bytes = 1024;
  request.max_stderr_bytes = 0;
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, std::string(1024, 'o'));
  EXPECT_TRUE(result.stderr_text.empty());
  EXPECT_TRUE(result.stdout_truncated);
  EXPECT_TRUE(result.stderr_truncated);
}

TEST(PortableProcess, HandlesChildClosingItsInput) {
  auto request = Request("exit");
  request.args.push_back("0");
  request.stdin_text.assign(1U << 20, 'x');
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
}

TEST(PortableProcess, HonorsAnExplicitDeadlineDuringContinuousOutput) {
  auto request = Request("busy");
  request.timeout = 100ms;
  request.max_stdout_bytes = 1024;
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_TRUE(result.timed_out);
  EXPECT_NE(result.exit_code, 0);
  EXPECT_LE(result.stdout_text.size(), 1024U);
#if defined(_WIN32)
  EXPECT_FALSE(result.terminated_by_signal);
  EXPECT_EQ(result.signal_number, 0);
#else
  EXPECT_TRUE(result.terminated_by_signal);
#endif
}

TEST(PortableProcess, DoesNotInventADeadline) {
  auto request = Request("pause");
  request.timeout.reset();
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
}

#if defined(_WIN32)
TEST(PortableProcess, DoesNotInheritUnrelatedHandles) {
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const HANDLE event = CreateEventW(&attributes, TRUE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  auto request = Request("inherited-handle");
  request.args.push_back(std::to_string(reinterpret_cast<uintptr_t>(event)));
  const auto result = pafio::RunProcessChecked(request);
  CloseHandle(event);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, "closed");
}

TEST(PortableProcess, TerminatesTheDescendantJobOnTimeout) {
  auto request = Request("spawn-descendant");
  request.timeout = 1s;
  const auto result = pafio::RunProcessChecked(request);
  EXPECT_TRUE(result.timed_out);
  ASSERT_TRUE(result.stdout_text.starts_with("pid="));
  const auto child_id = static_cast<DWORD>(std::stoul(result.stdout_text.substr(4)));
  const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, child_id);
  if (process) {
    EXPECT_EQ(WaitForSingleObject(process, 5000), WAIT_OBJECT_0);
    CloseHandle(process);
  } else {
    EXPECT_EQ(GetLastError(), ERROR_INVALID_PARAMETER);
  }
}
#endif

int Run(const std::vector<std::string> &arguments) {
  executable = pafio::ProcessPathString(fs::absolute(PathFromUtf8(arguments.at(0))));
  if (arguments.size() == 2 && arguments[1] == "--machine-info=json") {
    std::cout << kCompilerProbeMachineInfo << '\n';
    return 0;
  }
  if (arguments.size() >= 3 && arguments[1] == "--process-fixture") return Fixture(arguments);
  std::vector<std::string> mutable_arguments = arguments;
  std::vector<char *> pointers;
  for (std::string &argument : mutable_arguments) pointers.push_back(argument.data());
  pointers.push_back(nullptr);
  int count = static_cast<int>(arguments.size());
  testing::InitGoogleTest(&count, pointers.data());
  return RUN_ALL_TESTS();
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv) {
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  std::vector<std::string> arguments;
  for (int i = 0; i < argc; ++i) arguments.push_back(Utf8(argv[i]));
  return Run(arguments);
}
#else
int main(int argc, char **argv) {
  return Run({argv, argv + argc});
}
#endif
