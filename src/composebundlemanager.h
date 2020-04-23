#ifndef COMPOSE_BUNDLES_H_
#define COMPOSE_BUNDLES_H_

#include "package_manager/ostreemanager.h"

#define PACKAGE_MANAGER_COMPOSEBUNDLE "ostree+compose_bundles"

class ComposeBundleConfig {
 public:
  ComposeBundleConfig(const PackageConfig &pconfig);

  std::vector<std::string> bundles;
  boost::filesystem::path bundles_root;
  boost::filesystem::path compose_bin{"/usr/bin/docker-compose"};
  bool docker_prune{true};
};

class ComposeBundleManager : public OstreeManager {
 public:
  ComposeBundleManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                       const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http)
      : OstreeManager(pconfig, bconfig, storage, http), cfg_(pconfig) {}

  std::string name() const override { return PACKAGE_MANAGER_COMPOSEBUNDLE; };

 private:
  ComposeBundleConfig cfg_;
};

#endif  // COMPOSE_BUNDLES_H_
