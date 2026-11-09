#include "PafioRegistryClient/Client.hpp"
#include "PafioSecurity/TufVerifier.hpp"
#include "BuildTestSupport.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
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

void
RunTar(const std::vector<std::string> &args, const std::string &context) {
  const pafio::ProcessResult result = pafio::RunProcess<pafio::FetchError>({
    .program = pafio::ResolvedTarPath(),
    .args = args,
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

struct TufFixture
{
  std::map<std::string, std::string> objects;
  fs::path artifact_path;
  std::string artifact_relative_path;
  std::string artifact_sha256;
  std::string index_relative_path;
  json index_record;
};

// Builds the minimal registry-v2 trust set the verifier walks for one package:
// config + root + timestamp + snapshot + checkpoint + namespace targets plus
// the package index file and a real source artifact tar, all genuinely signed
// with the test key. The targets release entry binds index_record_sha256,
// source_artifact_sha256, and source_artifact_path the way the registry
// publisher does.
TufFixture
BuildFixtureObjects(const fs::path &work_dir, const bool symlink_member = false) {
  const TestKeyPair key = GenerateTestKeyPair(work_dir);
  TufFixture fixture;
  std::map<std::string, std::string> &objects = fixture.objects;

  const fs::path package_dir = work_dir / "package-source";
  WriteFile(
    package_dir / "pafio.toml",
    "[pafio]\nmanifest-version = 1\n\n[package]\nname = \"acme/util\"\nversion = \"1.0.0\"\nedition = \"2026\"\n\n[lib]\npath = \"src/lib.styio\"\n");
  WriteFile(package_dir / "src/lib.styio", "// tuf fixture library\n");
  if (symlink_member) {
    // A link member keeps the TUF chain and index record internally
    // consistent while the artifact itself violates the file/directory-only
    // member policy checked before extraction.
    fs::create_directory_symlink("/etc", package_dir / "src" / "escape-link");
  }
  fixture.artifact_path = work_dir / "acme-util-1.0.0.pafio.src.tar";
  RunTar(
    {"-cf", pafio::ProcessPathString(fixture.artifact_path), "-C", pafio::ProcessPathString(package_dir), "pafio.toml", "src"},
    "tar fixture archive");
  fixture.artifact_sha256 = pafio::Sha256File(fixture.artifact_path);
  fixture.artifact_relative_path = "artifacts/source/" + fixture.artifact_sha256 + ".pafio.src.tar";

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

  fixture.index_relative_path = "index/acme.util.jsonl";
  fixture.index_record = {
    {"schema_version", 1},
    {"package", "acme/util"},
    {"version", "1.0.0"},
    {"release_revision", 1},
    {"published_at", "2026-01-01T00:00:00Z"},
    {"publisher_id", "pafio-tests"},
    {"yanked", false},
    {"deprecated_message", ""},
    {"source_artifact",
     {{"sha256", fixture.artifact_sha256},
      {"size_bytes", fs::file_size(fixture.artifact_path)},
      {"path", fixture.artifact_relative_path},
      {"archive_format", "tar"},
      {"compression", "none"}}},
    {"binary_artifacts", json::array()},
    {"dependencies", json::array()},
    {"dev_dependencies", json::array()},
    {"features", {{"default", json::array()}, {"optional", json::array()}}},
    {"manifest_digest", pafio::Sha256Text("fixture manifest")},
    {"metadata_digest", pafio::Sha256Text("fixture metadata")},
  };
  objects[fixture.index_relative_path] = fixture.index_record.dump() + '\n';

  const json package_entry = {
    {"index_path", fixture.index_relative_path},
    {"latest_version", "1.0.0"},
    {"releases",
     {{"1.0.0",
       {{"release_revision", 1},
        {"index_record_sha256", pafio::Sha256Text(pafio::CanonicalJsonBytes(fixture.index_record))},
        {"source_artifact_sha256", fixture.artifact_sha256},
        {"source_artifact_path", fixture.artifact_relative_path},
        {"binary_artifact_count", 0}}}}},
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
    {"meta",
     {{"trust/targets/acme.json", MetaFor(objects["trust/targets/acme.json"], false)},
      {fixture.index_relative_path, MetaFor(objects[fixture.index_relative_path], false)}}},
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

  return fixture;
}

void
WriteFixtureTree(const std::map<std::string, std::string> &objects, const fs::path &root) {
  for (const auto &[relative, bytes] : objects) {
    const fs::path target = root / relative;
    fs::create_directories(target.parent_path());
    WriteFile(target, bytes);
  }
}

// Writes the signed metadata tree plus the source artifact the index record
// points at, so a file:// registry serves a fully materializable package.
void
WriteFixtureRegistry(const TufFixture &fixture, const fs::path &root) {
  WriteFixtureTree(fixture.objects, root);
  const fs::path artifact_target = root / fixture.artifact_relative_path;
  fs::create_directories(artifact_target.parent_path());
  fs::copy_file(fixture.artifact_path, artifact_target);
}

// Packs a single-manifest archive distinct from the fixture artifact so a
// tampered index record can point at a real attacker-controlled tarball.
fs::path
BuildFixtureArtifact(const fs::path &work_dir, const std::string &label, std::string &sha256_out) {
  const fs::path package_dir = work_dir / label;
  WriteFile(
    package_dir / "pafio.toml",
    "[pafio]\nmanifest-version = 1\n\n[package]\nname = \"acme/util\"\nversion = \"1.0.0\"\nedition = \"2026\"\n\n[lib]\npath = \"src/lib.styio\"\n");
  const fs::path artifact_path = work_dir / (label + ".tar");
  RunTar(
    {"-cf", pafio::ProcessPathString(artifact_path), "-C", pafio::ProcessPathString(package_dir), "pafio.toml"},
    "tar fixture archive");
  sha256_out = pafio::Sha256File(artifact_path);
  return artifact_path;
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
  const auto objects = BuildFixtureObjects(work_dir).objects;
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
  const auto objects = BuildFixtureObjects(work_dir).objects;
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
  const auto objects = BuildFixtureObjects(work_dir).objects;
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
  WriteFixtureTree(BuildFixtureObjects(work_dir / "signing").objects, registry);

  // Tampering with the signed timestamp invalidates its signature and fails
  // the chain before any package cache write can happen.
  const fs::path timestamp_path = registry / "trust" / "timestamp.json";
  WriteFile(timestamp_path, TamperedEnvelope(ReadFile(timestamp_path)));

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const fs::path blob_root = pafio::RegistryBlobCacheRoot(root / ".pafio-home");
  EXPECT_THROW(
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false),
      pafio::FetchError);
  EXPECT_TRUE(!fs::exists(blob_root) || fs::is_empty(blob_root));
}

TEST(TufVerifierTests, MaterializeResolvesPackageThroughSignedReleaseBinding)
{
  const fs::path work_dir = MakeTempDir("tuf-index-binding");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing");
  WriteFixtureRegistry(fixture, registry);

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const pafio::RegistryMaterializationResult resolved =
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
  EXPECT_EQ(resolved.package_name, "acme/util");
  EXPECT_EQ(resolved.version, "1.0.0");
  EXPECT_EQ(resolved.sha256, fixture.artifact_sha256);
  EXPECT_TRUE(fs::exists(resolved.snapshot_root / "pafio.toml"));
}

TEST(TufVerifierTests, MaterializeRebuildsCheckoutFromVerifiedBlobWithoutRefetch)
{
  const fs::path work_dir = MakeTempDir("tuf-checkout-repair");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing");
  WriteFixtureRegistry(fixture, registry);

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const pafio::RegistryMaterializationResult first =
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
  ASSERT_TRUE(fs::exists(first.snapshot_root / "pafio.toml"));

  const fs::path pafio_home_path = root / ".pafio-home";
  const fs::path blob_path =
      pafio::RegistryBlobCacheRoot(pafio_home_path) /
      fixture.artifact_sha256.substr(0U, 2U) /
      fixture.artifact_sha256.substr(2U, 2U) /
      (fixture.artifact_sha256 + ".tar");
  const fs::path checkout_root =
      pafio::RegistryCheckoutRoot(pafio_home_path) / "acme" / "util" / "1.0.0" /
      fixture.artifact_sha256;
  ASSERT_TRUE(fs::exists(blob_path));
  ASSERT_TRUE(fs::exists(checkout_root / ".pafio-snapshot-ready"));

  // A missing checkout marker must not discard the verified blob: the
  // registry-side artifact is removed too, so any re-fetch attempt fails and
  // only a re-unpack from the cached blob can satisfy the materialization.
  fs::remove(checkout_root / ".pafio-snapshot-ready");
  fs::remove(registry / fixture.artifact_relative_path);

  const pafio::RegistryMaterializationResult second =
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
  EXPECT_EQ(second.snapshot_root, first.snapshot_root);
  EXPECT_TRUE(fs::exists(second.snapshot_root / "pafio.toml"));
  EXPECT_TRUE(fs::exists(checkout_root / ".pafio-snapshot-ready"));
}

TEST(TufVerifierTests, MaterializeRefetchesOnlyTheCorruptedBlobAndKeepsCheckout)
{
  const fs::path work_dir = MakeTempDir("tuf-blob-repair");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing");
  WriteFixtureRegistry(fixture, registry);

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const pafio::RegistryMaterializationResult first =
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
  ASSERT_TRUE(fs::exists(first.snapshot_root / "pafio.toml"));

  const fs::path pafio_home_path = root / ".pafio-home";
  const fs::path blob_path =
      pafio::RegistryBlobCacheRoot(pafio_home_path) /
      fixture.artifact_sha256.substr(0U, 2U) /
      fixture.artifact_sha256.substr(2U, 2U) /
      (fixture.artifact_sha256 + ".tar");
  const fs::path checkout_root =
      pafio::RegistryCheckoutRoot(pafio_home_path) / "acme" / "util" / "1.0.0" /
      fixture.artifact_sha256;
  ASSERT_TRUE(fs::exists(blob_path));

  // A blob that fails size/digest verification is fetched again, but the
  // still-valid checkout must survive the repair.
  WriteFile(checkout_root / "sentinel.keep", "keep");
  WriteFile(blob_path, "corrupted cache bytes");

  const pafio::RegistryMaterializationResult second =
      pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
  EXPECT_EQ(second.sha256, fixture.artifact_sha256);
  EXPECT_EQ(pafio::Sha256File(blob_path), fixture.artifact_sha256);
  EXPECT_TRUE(fs::exists(checkout_root / "sentinel.keep"));
  EXPECT_TRUE(fs::exists(second.snapshot_root / "pafio.toml"));
}

TEST(TufVerifierTests, MaterializeRefusesSubstitutedIndexRecordWithoutCacheWrite)
{
  const fs::path work_dir = MakeTempDir("tuf-index-swap");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing");
  WriteFixtureRegistry(fixture, registry);

  // The signed chain is untouched: a write-capable read plane replaces the
  // index record for acme/util@1.0.0 with one pointing at an attacker
  // tarball, so every role signature stays valid and only the signed release
  // binding can refuse the substitution.
  std::string attacker_sha256;
  const fs::path attacker_tar =
      BuildFixtureArtifact(work_dir / "signing", "attacker-source", attacker_sha256);
  const std::string attacker_relative =
      "artifacts/source/" + attacker_sha256 + ".pafio.src.tar";
  const fs::path attacker_target = registry / attacker_relative;
  fs::create_directories(attacker_target.parent_path());
  fs::copy_file(attacker_tar, attacker_target);
  json swapped = fixture.index_record;
  swapped["source_artifact"]["sha256"] = attacker_sha256;
  swapped["source_artifact"]["size_bytes"] = fs::file_size(attacker_tar);
  swapped["source_artifact"]["path"] = attacker_relative;
  WriteFile(registry / fixture.index_relative_path, swapped.dump() + '\n');

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const fs::path blob_root = pafio::RegistryBlobCacheRoot(root / ".pafio-home");
  try {
    (void)pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
    FAIL() << "expected FetchError for a substituted index record";
  }
  catch (const pafio::FetchError &error) {
    EXPECT_NE(
        std::string(error.what()).find("does not match the signed release metadata"),
        std::string::npos)
        << error.what();
  }
  EXPECT_TRUE(!fs::exists(blob_root) || fs::is_empty(blob_root));
}

TEST(TufVerifierTests, MaterializeRefusesDuplicateIndexRecords)
{
  const fs::path work_dir = MakeTempDir("tuf-index-duplicate");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing");
  WriteFixtureRegistry(fixture, registry);

  // A second record for the same package/version must not shadow the signed
  // one: append an attacker-controlled duplicate after the signed record.
  std::string attacker_sha256;
  const fs::path attacker_tar =
      BuildFixtureArtifact(work_dir / "signing", "attacker-source", attacker_sha256);
  json duplicate = fixture.index_record;
  duplicate["source_artifact"]["sha256"] = attacker_sha256;
  duplicate["source_artifact"]["size_bytes"] = fs::file_size(attacker_tar);
  duplicate["source_artifact"]["path"] =
      "artifacts/source/" + attacker_sha256 + ".pafio.src.tar";
  const fs::path index_path = registry / fixture.index_relative_path;
  WriteFile(index_path, ReadFile(index_path) + duplicate.dump() + '\n');

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  try {
    (void)pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
    FAIL() << "expected FetchError for duplicate index records";
  }
  catch (const pafio::FetchError &error) {
    EXPECT_NE(std::string(error.what()).find("duplicate records"), std::string::npos)
        << error.what();
  }
}

TEST(TufVerifierTests, MaterializeRejectsNonFileArchiveMemberBeforeExtraction)
{
  const fs::path work_dir = MakeTempDir("tuf-archive-member-type");
  const fs::path root = work_dir / "case";
  const ScopedEnvVar pafio_home("PAFIO_HOME", (root / ".pafio-home").string());
  const fs::path registry = root / "registry";
  // The TUF chain and signed release binding are fully consistent; the
  // artifact itself carries a symlink member, which the pre-extraction
  // listing check must refuse.
  const TufFixture fixture = BuildFixtureObjects(work_dir / "signing", true);
  WriteFixtureRegistry(fixture, registry);

  const std::string registry_root = std::string("file://") + fs::absolute(registry).generic_string();
  const fs::path pafio_home_path = root / ".pafio-home";
  const fs::path checkout_root =
      pafio::RegistryCheckoutRoot(pafio_home_path) / "acme" / "util" / "1.0.0" /
      fixture.artifact_sha256;
  try {
    (void)pafio::MaterializeRegistryPackage(registry_root, "acme/util", "1.0.0", false);
    FAIL() << "expected CacheError for a non-file archive member";
  }
  catch (const pafio::CacheError &error) {
    EXPECT_NE(std::string(error.what()).find("member type"), std::string::npos)
        << error.what();
  }
  EXPECT_FALSE(fs::exists(checkout_root / "pafio.toml"));
  EXPECT_FALSE(fs::exists(checkout_root / ".pafio-snapshot-ready"));
}
