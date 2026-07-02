#include "PafioCore/Process.hpp"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(ProcessTests, CapturesStdoutAndStderrWithoutDeadlocking)
{
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "/bin/sh",
      .args = {
          "-c",
          "i=0; while [ \"$i\" -lt 32768 ]; do printf x >&2; i=$((i + 1)); done; printf ok",
      },
      .search_path = false,
      .timeout = 5s,
      .max_stderr_bytes = 1U << 20,
      .error_context = "process test",
  });

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
  EXPECT_EQ(result.stdout_text, "ok");
  EXPECT_EQ(result.stderr_text.size(), 32768U);
}

TEST(ProcessTests, TimesOutAndTerminatesProcessGroup)
{
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "/bin/sh",
      .args = {"-c", "sleep 2"},
      .search_path = false,
      .timeout = 100ms,
      .error_context = "timeout test",
  });

  EXPECT_TRUE(result.timed_out);
  EXPECT_TRUE(result.terminated_by_signal);
  EXPECT_EQ(result.signal_number, SIGKILL);
}

TEST(ProcessTests, TimesOutWhenChildKeepsProducingOutput)
{
  const auto start = std::chrono::steady_clock::now();
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "python3",
      .args = {
          "-c",
          "import sys, time\n"
          "end = time.monotonic() + 2\n"
          "while time.monotonic() < end:\n"
          "    sys.stdout.write('x' * 4096)\n"
          "    sys.stdout.flush()\n"
          "    time.sleep(0.001)\n",
      },
      .timeout = 100ms,
      .max_stdout_bytes = 1024,
      .error_context = "busy-output timeout test",
  });
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  EXPECT_TRUE(result.timed_out);
  EXPECT_TRUE(result.terminated_by_signal);
  EXPECT_EQ(result.signal_number, SIGKILL);
  EXPECT_LT(elapsed, 1500ms);
}

TEST(ProcessTests, TracksSignalTermination)
{
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "/bin/sh",
      .args = {"-c", "kill -TERM $$"},
      .search_path = false,
      .timeout = 5s,
      .error_context = "signal test",
  });

  EXPECT_TRUE(result.terminated_by_signal);
  EXPECT_EQ(result.signal_number, SIGTERM);
  EXPECT_EQ(result.exit_code, 128 + SIGTERM);
}

TEST(ProcessTests, ClearsEnvironmentBeforeApplyingOverrides)
{
  setenv("PAFIO_PARENT_ONLY_MARKER", "leak", 1);
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "/bin/sh",
      .args = {
          "-c",
          "if [ \"${PAFIO_PARENT_ONLY_MARKER+x}\" = x ]; then exit 7; fi; "
          "if [ \"$PAFIO_CHILD_MARKER\" != present ]; then exit 8; fi; "
          "printf ok",
      },
      .search_path = false,
      .environment_overrides = {{"PAFIO_CHILD_MARKER", std::optional<std::string>{"present"}}},
      .clear_environment = true,
      .timeout = 5s,
      .error_context = "clear environment test",
  });
  unsetenv("PAFIO_PARENT_ONLY_MARKER");

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, "ok");
}

TEST(ProcessTests, MarksTruncatedOutput)
{
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "/bin/sh",
      .args = {
          "-c",
          "i=0; while [ \"$i\" -lt 8192 ]; do printf y; printf z >&2; i=$((i + 1)); done",
      },
      .search_path = false,
      .timeout = 5s,
      .max_stdout_bytes = 1024,
      .max_stderr_bytes = 1024,
      .error_context = "truncate test",
  });

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_truncated);
  EXPECT_TRUE(result.stderr_truncated);
  EXPECT_EQ(result.stdout_text.size(), 1024U);
  EXPECT_EQ(result.stderr_text.size(), 1024U);
}

TEST(ProcessTests, StreamsLargeStdinWhileDrainingStdout)
{
  const std::string stdin_text(1U << 18, 'i');
  const pafio::ProcessResult result = pafio::RunProcess({
      .program = "python3",
      .args = {
          "-c",
          "import sys\n"
          "sys.stdout.write('o' * (1 << 18))\n"
          "sys.stdout.flush()\n"
          "data = sys.stdin.read()\n"
          "print(len(data))\n",
      },
      .timeout = 5s,
      .stdin_text = stdin_text,
      .error_context = "bidirectional process test",
  });

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
  EXPECT_FALSE(result.terminated_by_signal);
  EXPECT_FALSE(result.stdout_truncated);
  EXPECT_FALSE(result.stderr_truncated);
  EXPECT_NE(result.stdout_text.find("262144"), std::string::npos);
}
