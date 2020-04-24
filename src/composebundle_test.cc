#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include "test_utils.h"

#include "composebundlemanager.h"

static boost::filesystem::path test_sysroot;

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
