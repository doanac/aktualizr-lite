#include "composebundlemanager.h"

ComposeBundleConfig::ComposeBundleConfig(const PackageConfig &pconfig) {
  const std::map<std::string, std::string> raw = pconfig.extra;

  if (raw.count("compose_bundles") == 1) {
    std::string val = raw.at("compose_bundles");
    if (val.length() > 0) {
      // token_compress_on allows lists like: "foo,bar", "foo, bar", or "foo bar"
      boost::split(bundles, val, boost::is_any_of(", "), boost::token_compress_on);
    }
  }
  if (raw.count("compose_bundles_root") == 1) {
    bundles_root = raw.at("compose_bundles_root");
  }
  if (raw.count("docker_compose_bin") == 1) {
    compose_bin = raw.at("docker_compose_bin");
  }

  if (raw.count("docker_prune") == 1) {
    std::string val = raw.at("docker_prune");
    boost::algorithm::to_lower(val);
    docker_prune = val != "0" && val != "false";
  }
}

std::vector<std::pair<std::string, std::string>> ComposeBundleManager::getBundles(const Uptane::Target &t) const {
  std::vector<std::pair<std::string, std::string>> bundles;

  auto target_bundles = t.custom_data()["docker_compose_bundles"];
  for (Json::ValueIterator i = target_bundles.begin(); i != target_bundles.end(); ++i) {
    if ((*i).isObject() && (*i).isMember("uri")) {
      for (const auto &bundle : cfg_.bundles) {
        if (i.key().asString() == bundle) {
          bundles.emplace_back(bundle, (*i)["uri"].asString());
          break;
        }
      }
    } else {
      LOG_ERROR << "Invalid custom data for docker_compose_bundle: " << i.key().asString() << " -> " << *i;
    }
  }

  return bundles;
}
