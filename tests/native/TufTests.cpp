#include "PafioRegistryClient/Client.hpp"
#include "PafioSecurity/TufVerifier.hpp"
#include "BuildTestSupport.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioSecurity/CanonicalJson.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
using pafio::testsupport::MakeTempDir;
using pafio::testsupport::ReadFile;
using pafio::testsupport::ScopedEnvVar;
using pafio::testsupport::WriteFile;

namespace
{

// Each run generates a throwaway Ed25519 keypair through the same openssl
// tooling the verifier uses; no private key material is committed.
constexpr std::string_view kTestKeyId = "pafio-test-root-key";
constexpr std::string_view kFarFutureExpiry = "2999-01-01T00:00:00Z";

std::string
Base64Encode(std::string_view bytes) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (size_t i = 0; i < bytes.size(); i += 3U) {
    const unsigned value =
        (static_cast<unsigned char>(bytes[i]) << 16U) |
        (i + 1U < bytes.size() ? static_cast<unsigned char>(bytes[i + 1U]) << 8U : 0U) |
        (i + 2U < bytes.size() ? static_cast<unsigned char>(bytes[i + 2U]) : 0U);
    out.push_back(kTable[(value >> 18U) & 63U]);
    out.push_back(kTable[(value >> 12U) & 63U]);
    out.push_back(i + 1U < bytes.size() ? kTable[(value >> 6U) & 63U] : '=');
    out.push_back(i + 2U < bytes.size() ? kTable[value & 63U] : '=');
  }
  return out;
}

struct TestKeyPair
{
  fs::path private_key_path;
  std::string public_key_pem;
};

void
RunOpenSsl(const std::vector<std::string> &args, const std::string &context) {
  std::vector<std::string> full_args = args;
  const pafio::ProcessResult result = pafio::RunProcess<pafio::FetchError>({
    .program = pafio::ResolvedOpenSslPath(),
    .args = full_args,
    .search_path = false,
    .timeout = std::chrono::seconds{30},
    .error_context = context,
  });
  if (result.exit_code != 0) {
    throw pafio::FetchError(context + ": " + pafio::DescribeProcessFailure(result));
  }
}

TestKeyPair
GenerateTestKeyPair(const fs::path &work_dir) {
  fs::create_directories(work_dir);
  const fs::path private_path = work_dir / "fixture.key";
  const fs::path public_path = work_dir / "fixture.pub";
  RunOpenSsl(
    {"genpkey", "-algorithm", "ed25519", "-out", pafio::ProcessPathString(private_path)},
    "ed25519 test keygen");
  RunOpenSsl(
    {"pkey", "-in", pafio::ProcessPathString(private_path), "-pubout", "-out", pafio::ProcessPathString(public_path)},
    "ed25519 test pubkey export");
  return {.private_key_path = private_path, .public_key_pem = ReadFile(public_path)};
}

std::string
Ed25519Sign(const fs::path &key_path, const fs::path &work_dir, std::string_view message) {
  const fs::path message_path = work_dir / "fixture.message";
  const fs::path signature_path = work_dir / "fixture.sig";
  WriteFile(message_path, std::string(message));
  RunOpenSsl(
    {"pkeyutl",
     "-sign",
     "-inkey",
     pafio::ProcessPathString(key_path),
     "-rawin",
     "-in",
     pafio::ProcessPathString(message_path),
     "-out",
     pafio::ProcessPathString(signature_path)},
    "ed25519 test sign");
  return Base64Encode(ReadFile(signature_path));
}

json
SignedEnvelope(const json &signed_payload, const TestKeyPair &key, const fs::path &work_dir) {
  return {
    {"signed", signed_payload},
    {"signatures",
     json::array(
       {json{{"keyid", kTestKeyId},
             {"sig", Ed25519Sign(key.private_key_path, work_dir, pafio::CanonicalJsonBytes(signed_payload))}}})},
  };
}

json
MetaFor(const std::string &bytes, const bool with_length) {
  json meta{{"hashes", json{{"sha256", pafio::Sha256Text(bytes)}}}};
  if (with_length) {
    meta["length"] = static_cast<int>(bytes.size());
  }
  return meta;
}

json
RolePolicy() {
  return {{"keyids", json::array({kTestKeyId})}, {"threshold", 1}};
}

// Builds the minimal registry-v2 trust set the verifier walks for one package:
// config + root + timestamp + snapshot + checkpoint + namespace targets, all
// genuinely signed with the test key.
std::map<std::string, std::string>
BuildFixtureObjects(const fs::path &work_dir) {
  const TestKeyPair key = GenerateTestKeyPair(work_dir);
  std::map<std::string, std::string> objects;

  objects["config.json"] = json{
    {"protocol", "pafio-static-registry"},
    {"protocol_version", 2},
    {"capabilities",
     {{"append_only_index", true},
      {"source_artifacts", true},
      {"binary_artifacts", true},
      {"transparency_log", true}}},
  }.dump(2) + '\n';

  const json root_signed = {
    {"type", "root"},
    {"spec_version", "1"},
    {"version", 1},
    {"expires", kFarFutureExpiry},
    {"keys",
     {{std::string(kTestKeyId),
       {{"keytype", "ed25519"},
        {"scheme", "ed25519"},
        {"keyval", {{"public", key.public_key_pem}}}}}}},
    {"roles",
     {{"root", RolePolicy()},
      {"timestamp", RolePolicy()},
      {"snapshot", RolePolicy()},
      {"targets", RolePolicy()},
      {"log", RolePolicy()}}},
  };
  objects["trust/root.json"] = SignedEnvelope(root_signed, key, work_dir).dump(2) + '\n';

  const json package_entry = {
    {"latest", "1.0.0"},
    {"versions", {{"1.0.0", {{"index", "index/acme.util.jsonl"}}}}},
  };
  const json targets_signed = {
    {"type", "targets"},
    {"spec_version", "1"},
    {"version", 1},
    {"expires", kFarFutureExpiry},
    {"namespace", "acme"},
    {"packages", {{"acme/util", package_entry}}},
  };
  objects["trust/targets/acme.json"] = SignedEnvelope(targets_signed, key, work_dir).dump(2) + '\n';

  const json checkpoint_signed = {
    {"type", "checkpoint"},
    {"spec_version", "1"},
    {"version", 1},
    {"expires", kFarFutureExpiry},
    {"tree_size", 1},
  };
  objects["log/checkpoint.json"] = SignedEnvelope(checkpoint_signed, key, work_dir).dump(2) + '\n';

  const json snapshot_signed = {
    {"type", "snapshot"},
    {"spec_version", "1"},
    {"version", 1},
    {"expires", kFarFutureExpiry},
    {"meta", {{"trust/targets/acme.json", MetaFor(objects["trust/targets/acme.json"], false)}}},
    {"log_meta", {{"log/checkpoint.json", MetaFor(objects["log/checkpoint.json"], false)}}},
  };
  objects["trust/snapshot.json"] = SignedEnvelope(snapshot_signed, key, work_dir).dump(2) + '\n';

  const json timestamp_signed = {
    {"type", "timestamp"},
    {"spec_version", "1"},
    {"version", 1},
    {"expires", kFarFutureExpiry},
    {"meta", {{"trust/snapshot.json", MetaFor(objects["trust/snapshot.json"], true)}}},
  };
  objects["trust/timestamp.json"] = SignedEnvelope(timestamp_signed, key, work_dir).dump(2) + '\n';

  return objects;
}

void
WriteFixtureTree(const std::map<std::string, std::string> &objects, const fs::path &root) {
  for (const auto &[relative, bytes] : objects) {
    const fs::path target = root / relative;
    fs::create_directories(target.parent_path());
    WriteFile(target, bytes);
  }
}

// Bumping the signed version both invalidates the signature and changes the
// bytes referenced by parent meta hashes, so it breaks every role uniformly.
std::string
TamperedEnvelope(const std::string &bytes) {
  json envelope = json::parse(bytes);
  envelope["signed"]["version"] = envelope["signed"]["version"].get<int>() + 1;
  return envelope.dump(2) + '\n';
}

// Strips "expires" from one role's signed payload, re-signs it with the
// fixture key, and repairs every parent meta hash so the missing expiry is the
// only remaining defect in the chain.
void
StripExpiryResignAndRepair(std::map<std::string, std::string> &objects, const fs::path &work_dir, const TestKeyPair &key, const std::string &role) {
  {
    json envelope = json::parse(objects[role]);
    envelope["signed"].erase("expires");
    objects[role] = SignedEnvelope(envelope["signed"], key, work_dir).dump(2) + '\n';
  }
  if (role == "trust/root.json" || role == "trust/timestamp.json") {
    return;
  }
  if (role != "trust/snapshot.json") {
    json snapshot = json::parse(objects["trust/snapshot.json"]);
    if (role == "log/checkpoint.json") {
      snapshot["signed"]["log_meta"]["log/checkpoint.json"] = MetaFor(objects[role], false);
    } else {
      snapshot["signed"]["meta"][role] = MetaFor(objects[role], false);
    }
    objects["trust/snapshot.json"] = SignedEnvelope(snapshot["signed"], key, work_dir).dump(2) + '\n';
  }
  json timestamp = json::parse(objects["trust/timestamp.json"]);
  timestamp["signed"]["meta"]["trust/snapshot.json"] = MetaFor(objects["trust/snapshot.json"], true);
  objects["trust/timestamp.json"] = SignedEnvelope(timestamp["signed"], key, work_dir).dump(2) + '\n';
}

}  // namespace

TEST(TufVerifierTests, AcceptsValidFixtureAndRejectsTamperedRoles)
{
  const fs::path work_dir = MakeTempDir("tuf-fixture");
  const auto objects = BuildFixtureObjects(work_dir);
  const pafio::TufVerifyResult ok = pafio::VerifyTufChainForPackage(objects, "acme/util", "acme", std::nullopt, "trust/targets/acme.json");
  ASSERT_TRUE(ok.ok) << ok.error_code << ": " << ok.error;

  for (const char *relative : {"trust/root.json", "trust/timestamp.json", "trust/snapshot.json", "trust/targets/acme.json"})
  {
    auto tampered = objects;
    tampered[relative] = TamperedEnvelope(tampered[relative]);
    const pafio::TufVerifyResult failed = pafio::VerifyTufChainForPackage(tampered, "acme/util", "acme", std::nullopt, "trust/targets/acme.json");
    EXPECT_FALSE(failed.ok) << relative;
    EXPECT_FALSE(failed.error_code.empty()) << relative;
  }
}

TEST(TufVerifierTests, RejectsRolesWithoutExpiry)
{
  const fs::path work_dir = MakeTempDir("tuf-missing-expires");
  const auto objects = BuildFixtureObjects(work_dir);
  const TestKeyPair key{.private_key_path = work_dir / "fixture.key", .public_key_pem = ReadFile(work_dir / "fixture.pub")};
  for (const char *role : {"trust/root.json", "trust/timestamp.json", "trust/snapshot.json", "trust/targets/acme.json", "log/checkpoint.json"})
  {
    auto mutated = objects;
    StripExpiryResignAndRepair(mutated, work_dir, key, role);
    const pafio::TufVerifyResult failed = pafio::VerifyTufChainForPackage(mutated, "acme/util", "acme", std::nullopt, "trust/targets/acme.json");
    EXPECT_FALSE(failed.ok) << role;
    EXPECT_FALSE(failed.error_code.empty()) << role;
  }
}

TEST(TufVerifierTests, RejectsReplayedMetadataBelowPersistedFloors)
{
  const fs::path work_dir = MakeTempDir("tuf-replay-floor");
  const auto objects = BuildFixtureObjects(work_dir);
  const pafio::TufVerifyResult ok = pafio::VerifyTufChainForPackage(
      objects, "acme/util", "acme", std::nullopt, "trust/targets/acme.json");
  ASSERT_TRUE(ok.ok) << ok.error_code << ": " << ok.error;
  ASSERT_GT(ok.timestamp_version, 0);
  ASSERT_GT(ok.snapshot_version, 0);

  // The same still-valid metadata verifies again at the recorded floor.
  EXPECT_TRUE(pafio::VerifyTufChainForPackage(
                  objects,
                  "acme/util",
                  "acme",
                  std::nullopt,
                  "trust/targets/acme.json",
                  ok.timestamp_version,
                  ok.snapshot_version)
                  .ok);

  // Once a newer chain was accepted, replaying these older but still
  // unexpired signed versions is refused as a regression.
  const pafio::TufVerifyResult replayed = pafio::VerifyTufChainForPackage(
      objects,
      "acme/util",
      "acme",
      std::nullopt,
      "trust/targets/acme.json",
      ok.timestamp_version + 1,
      ok.snapshot_version);
  EXPECT_FALSE(replayed.ok);
  EXPECT_EQ(replayed.error_code, "tuf.version_regression");
}

TEST(TufVerifierTests, MaterializeFailsClosedOnTamperedRootWithoutCacheWrite)
{
  const fs::path work_dir = MakeTempDir("tuf-materialize");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  WriteFixtureTree(BuildFixtureObjects(work_dir / "signing"), registry);

  // Tampering with the signed timestamp invalidates its signature and fails
  // the chain before any package cache write can happen.
  const fs::path timestamp_path = registry / "trust" / "timestamp.json";
  WriteFile(timestamp_path, TamperedEnvelope(ReadFile(timestamp_path)));

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const fs::path blob_root = root / ".pafio-home" / "cache" / "registry" / "blobs";
  EXPECT_THROW(
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false),
      pafio::FetchError);
  EXPECT_TRUE(!fs::exists(blob_root) || fs::is_empty(blob_root));
}
