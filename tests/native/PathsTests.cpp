#include <gtest/gtest.h>

#include <filesystem>
#include <optional>

#include "BuildTestSupport.hpp"
#include "PafioCore/Paths.hpp"

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

using pafio::testsupport::ScopedEnvVar;

TEST(PathsTests, ExplicitPafioHomeTakesPrecedenceOverHomeEnvironment)
{
  const ScopedEnvVar explicit_home("PAFIO_HOME", "pafio-tests-configured-home");
  const ScopedEnvVar home("HOME", "pafio-tests-user-home");

  const std::optional<fs::path> resolved = pafio::ResolveOptionalPafioHome();
  ASSERT_TRUE(resolved.has_value());

  const fs::path expected = pafio::CanonicalAbsolutePath(fs::path("pafio-tests-configured-home"));
  EXPECT_TRUE(*resolved == expected);
}

TEST(PathsTests, UsesHomeEnvironmentWhenPafioHomeIsEmpty)
{
  const ScopedEnvVar explicit_home("PAFIO_HOME", "");
  const ScopedEnvVar home("HOME", "pafio-tests-user-home");

  const std::optional<fs::path> resolved = pafio::ResolveOptionalPafioHome();
  ASSERT_TRUE(resolved.has_value());

  const fs::path expected = pafio::CanonicalAbsolutePath(fs::path("pafio-tests-user-home") / ".pafio");
  EXPECT_TRUE(*resolved == expected);
}

#if !defined(_WIN32)
TEST(PathsTests, FallsBackToPasswdHomeWhenEnvironmentIsUnset)
{
  const ScopedEnvVar explicit_home("PAFIO_HOME", "");
  const ScopedEnvVar home("HOME", "");

  const std::optional<fs::path> resolved = pafio::ResolveOptionalPafioHome();
  ASSERT_TRUE(resolved.has_value());

  const struct passwd *entry = ::getpwuid(::getuid());
  ASSERT_TRUE(entry != nullptr && entry->pw_dir != nullptr && entry->pw_dir[0] != '\0');
  const fs::path expected = pafio::CanonicalAbsolutePath(fs::path(entry->pw_dir) / ".pafio");
  EXPECT_TRUE(*resolved == expected);
}
#endif
