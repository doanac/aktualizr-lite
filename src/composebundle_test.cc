#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include "http/httpclient.h"
#include "test_utils.h"

#include "composebundlemanager.h"

static boost::filesystem::path test_sysroot;

static struct {
  int serial{0};
  std::string rev;
} ostree_deployment;
static std::string new_rev;

extern "C" OstreeDeployment* ostree_sysroot_get_booted_deployment(OstreeSysroot* self) {
  (void)self;
  static GObjectUniquePtr<OstreeDeployment> dep;

  dep.reset(ostree_deployment_new(0, "dummy-os", ostree_deployment.rev.c_str(), ostree_deployment.serial,
                                  ostree_deployment.rev.c_str(), ostree_deployment.serial));
  return dep.get();
}

extern "C" const char* ostree_deployment_get_csum(OstreeDeployment* self) {
  (void)self;
  return ostree_deployment.rev.c_str();
}

static void progress_cb(const Uptane::Target& target, const std::string& description, unsigned int progress) {
  (void)description;
  LOG_INFO << "progress_cb " << target << " " << progress;
}

TEST(ComposeBundles, Config) {
  Config config;
  config.pacman.type = "ostree+compose_bundles";
  config.pacman.sysroot = test_sysroot.string();
  config.pacman.extra["compose_bundles_root"] = "apps-root";
  config.pacman.extra["compose_bundles"] = "app1 app2";
  config.pacman.extra["docker_compose_bin"] = "compose";

  ComposeBundleConfig cfg(config.pacman);
  ASSERT_TRUE(cfg.docker_prune);
  ASSERT_EQ(2, cfg.bundles.size());
  ASSERT_EQ("app1", cfg.bundles[0]);
  ASSERT_EQ("app2", cfg.bundles[1]);
  ASSERT_EQ("apps-root", cfg.bundles_root);
  ASSERT_EQ("compose", cfg.compose_bin);

  config.pacman.extra["docker_prune"] = "0";
  cfg = ComposeBundleConfig(config.pacman);
  ASSERT_FALSE(cfg.docker_prune);

  config.pacman.extra["docker_prune"] = "FALSE";
  cfg = ComposeBundleConfig(config.pacman);
  ASSERT_FALSE(cfg.docker_prune);
}

struct TestClient {
  TestClient(const char* bundles, const Uptane::Target* installedTarget) {
    tempdir = std_::make_unique<TemporaryDirectory>();

    Config config;
    config.bootloader.reboot_sentinel_dir = tempdir->Path();
    config.pacman.type = PACKAGE_MANAGER_COMPOSEBUNDLE;
    config.pacman.sysroot = test_sysroot.string();
    config.pacman.extra["compose_bundles_root"] = (*tempdir / "bundles").native();
    config.pacman.extra["compose_bundles"] = bundles;
    config.pacman.extra["docker_compose_bin"] = "src/compose_fake.sh";
    config.pacman.extra["docker_prune"] = "0";
    config.storage.path = tempdir->Path();

    storage = INvStorage::newStorage(config.storage);
    if (installedTarget != nullptr) {
      storage->savePrimaryInstalledVersion(*installedTarget, InstalledVersionUpdateMode::kCurrent);
    }
    pacman = std_::make_unique<ComposeBundleManager>(config.pacman, config.bootloader, storage, nullptr);
    keys = std_::make_unique<KeyManager>(storage, config.keymanagerConfig());
    fetcher = std_::make_unique<Uptane::Fetcher>(config, std::make_shared<HttpClient>());
  }

  std::unique_ptr<TemporaryDirectory> tempdir;
  std::shared_ptr<INvStorage> storage;
  std::unique_ptr<ComposeBundleManager> pacman;
  std::unique_ptr<KeyManager> keys;
  std::unique_ptr<Uptane::Fetcher> fetcher;
};

TEST(ComposeBundles, getBundles) {
  TemporaryDirectory dir;
  auto mgr = TestClient("app1 app2", nullptr).pacman;

  std::string sha = Utils::readFile(test_sysroot / "ostree/repo/refs/heads/ostree/1/1/0", true);
  Json::Value target_json;
  target_json["hashes"]["sha256"] = sha;
  target_json["custom"]["targetFormat"] = "OSTREE";
  target_json["length"] = 0;
  target_json["custom"]["docker_compose_bundles"]["app1"]["uri"] = "n/a";
  target_json["custom"]["docker_compose_bundles"]["app2"]["uri"] = "N/A";
  Uptane::Target target("pull", target_json);

  auto bundles = mgr->getBundles(target);
  ASSERT_EQ(2, bundles.size());
  ASSERT_EQ("app1", bundles[0].first);
  ASSERT_EQ("n/a", bundles[0].second);
  ASSERT_EQ("app2", bundles[1].first);
  ASSERT_EQ("N/A", bundles[1].second);
}

TEST(ComposeBundles, fetch) {
  std::string sha = Utils::readFile(test_sysroot / "ostree/repo/refs/heads/ostree/1/1/0", true);
  Json::Value target_json;
  target_json["hashes"]["sha256"] = sha;
  target_json["custom"]["targetFormat"] = "OSTREE";
  target_json["length"] = 0;
  target_json["custom"]["docker_compose_bundles"]["app1"]["uri"] = "n/a";
  target_json["custom"]["docker_compose_bundles"]["app2"]["uri"] = "N/A";
  Uptane::Target target("pull", target_json);

  TestClient client("app2 doesnotexist", nullptr);  // only app2 can be fetched
  bool result = client.pacman->fetchTarget(target, *(client.fetcher), *(client.keys), progress_cb, nullptr);
  ASSERT_TRUE(result);
  std::string output = Utils::readFile(client.tempdir->Path() / "bundles/app2/download.log", true);
  ASSERT_EQ("download N/A", output);
  output = Utils::readFile(client.tempdir->Path() / "bundles/app2/config.log", true);
  ASSERT_EQ("config", output);
  output = Utils::readFile(client.tempdir->Path() / "bundles/app2/pull.log", true);
  ASSERT_EQ("pull", output);
  ASSERT_FALSE(boost::filesystem::exists(client.tempdir->Path() / "bundles/doesnotexist"));

  // Now do a quick check that we can handle a simple download failure
  target_json["custom"]["docker_compose_bundles"]["app2"]["uri"] = "FAILTEST";
  target = Uptane::Target("pull", target_json);
  result = client.pacman->fetchTarget(target, *(client.fetcher), *(client.keys), progress_cb, nullptr);
  ASSERT_FALSE(result);
}

TEST(ComposeBundles, handleRemovedApps) {
  // Configure a client for app1, app2, and app3
  TestClient client("app1 app2 app3", nullptr);
  auto bundles = client.tempdir->Path() / "bundles";

  // Create a target for both app2 (app3 is configured, but not in the targets)
  Json::Value target_json;
  target_json["custom"]["docker_compose_bundles"]["app1"]["uri"] = "";
  target_json["custom"]["docker_compose_bundles"]["app2"]["uri"] = "";
  Uptane::Target target("pull", target_json);

  // Make app2 and app3 look like they are installed:
  boost::filesystem::create_directories(bundles / "app2");
  boost::filesystem::create_directories(bundles / "app3");
  // Make an app we aren't configured for:
  boost::filesystem::create_directories(bundles / "BAD");

  client.pacman->handleRemovedApps(target);
  ASSERT_FALSE(boost::filesystem::exists(bundles / "BAD"));
  ASSERT_FALSE(boost::filesystem::exists(bundles / "app3"));
  ASSERT_TRUE(boost::filesystem::exists(bundles / "app2"));
}

TEST(ComposeBundles, install) {
  // Trick system into not doing an OSTreeManager install
  std::string sha = Utils::readFile(test_sysroot / "ostree/repo/refs/heads/ostree/1/1/0", true);
  ostree_deployment.serial = 1;
  ostree_deployment.rev = sha;

  Json::Value target_json;
  target_json["hashes"]["sha256"] = sha;
  target_json["custom"]["docker_compose_bundles"]["app1"]["uri"] = "";
  Uptane::Target target("pull", target_json);

  TestClient client("app1", &target);

  // We are't doing a fetch, so we have to make this directory so that the
  // compose_fake script can run:
  boost::filesystem::create_directories(client.tempdir->Path() / "bundles/app1");
  ASSERT_EQ(data::ResultCode::Numeric::kOk, client.pacman->install(target).result_code.num_code);
  std::string output = Utils::readFile(client.tempdir->Path() / "bundles/app1/up.log", true);
  ASSERT_EQ("up --remove-orphans -d", output);
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (argc != 2) {
    std::cerr << "Error: " << argv[0] << " requires the path to an OSTree sysroot as an input argument.\n";
    return EXIT_FAILURE;
  }

  TemporaryDirectory temp_dir;
  // Utils::copyDir doesn't work here. Complaints about non existent symlink path
  int r = system((std::string("cp -r ") + argv[1] + std::string(" ") + temp_dir.PathString()).c_str());
  if (r != 0) {
    return -1;
  }
  test_sysroot = (temp_dir.Path() / "ostree_repo").string();

  return RUN_ALL_TESTS();
}
#endif
