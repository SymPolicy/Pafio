#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>

#include "PafioCore/AtomicFile.hpp"
#include "PafioCore/FileLock.hpp"
#include "PafioCore/Process.hpp"

using namespace std::chrono_literals;

TEST(ProcessTests, CapturesStdoutAndStderrWithoutDeadlocking) {
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

TEST(ProcessTests, TimesOutAndTerminatesProcessGroup) {
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

TEST(ProcessTests, TimesOutWhenChildKeepsProducingOutput) {
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
    std::chrono::steady_clock::now() - start
  );

  EXPECT_TRUE(result.timed_out);
  EXPECT_TRUE(result.terminated_by_signal);
  EXPECT_EQ(result.signal_number, SIGKILL);
  EXPECT_LT(elapsed, 1500ms);
}

TEST(ProcessTests, TracksSignalTermination) {
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

TEST(ProcessTests, MarksTruncatedOutput) {
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

TEST(ProcessTests, StreamsLargeStdinWhileDrainingStdout) {
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

TEST(ProcessTests, ChildClosingStdinDoesNotTerminateParentWithSigpipe) {
  const pafio::ProcessResult result = pafio::RunProcess({
    .program = "/bin/sh",
    .args = {"-c", "exit 0"},
    .search_path = false,
    .timeout = 5s,
    .stdin_text = std::string(1U << 20, 'x'),
    .error_context = "closed stdin process test",
  });

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.timed_out);
}

TEST(AtomicFileTests, SupportsAFileNameWithoutAParentDirectory) {
  const std::filesystem::path path =
    "pafio-atomic-relative-" + std::to_string(static_cast<long long>(getpid())) + ".tmp";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  ASSERT_NO_THROW(pafio::AtomicWriteFile(path, "complete\n"));
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  EXPECT_EQ(
    std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
    "complete\n"
  );

  std::filesystem::remove(path, ignored);
}

TEST(AtomicFileTests, ExclusiveCreateRefusesAnExistingNameWithoutTruncating) {
  const std::filesystem::path directory =
    std::filesystem::temp_directory_path()
    / ("pafio-exclusive-" + std::to_string(static_cast<long long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path candidate = directory / "candidate.tmp";

  pafio::ExclusiveTempFile first;
  ASSERT_TRUE(pafio::TryCreateExclusiveFile(candidate, first));
  ASSERT_NO_THROW(pafio::WriteTemporaryFileFully(first, "keep", 4));
  ASSERT_NO_THROW(pafio::CloseTemporaryFile(first));

  // A second exclusive create at the same name must report the collision and
  // leave the existing bytes alone — not truncate through a pre-planted entry.
  pafio::ExclusiveTempFile second;
  EXPECT_FALSE(pafio::TryCreateExclusiveFile(candidate, second));
  pafio::DiscardTemporaryFile(second);

  std::ifstream input(candidate, std::ios::binary);
  ASSERT_TRUE(input);
  EXPECT_EQ(
    std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
    "keep"
  );

  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
}

TEST(FileLockTests, MoveAssignmentReleasesThePreviouslyOwnedLock) {
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / ("pafio-file-lock-move-" + std::to_string(static_cast<long long>(getpid())));
  std::filesystem::create_directories(root);

  pafio::FileLockGuard first =
    pafio::AcquireFileLock(root / "first", pafio::FileLockScope::kCache, 0ms);
  pafio::FileLockGuard second =
    pafio::AcquireFileLock(root / "second", pafio::FileLockScope::kCache, 0ms);
  second = std::move(first);

  EXPECT_FALSE(static_cast<bool>(first));
  EXPECT_TRUE(static_cast<bool>(second));
  EXPECT_NO_THROW(
    static_cast<void>(
      pafio::AcquireFileLock(root / "second", pafio::FileLockScope::kCache, 0ms)
    )
  );

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}
