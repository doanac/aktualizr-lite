#include "composebundlemanager.h"
#include <boost/process.hpp>

struct ComposeBundle {
  ComposeBundle(std::string name, const ComposeBundleConfig &config)
      : name_(std::move(name)),
        root_(config.bundles_root / name_),
        compose_(boost::filesystem::canonical(config.compose_bin).string() + " ") {}

  // Utils::shell isn't interactive. The compose commands can take a few
  // seconds to run, so we use boost::process:system to stream it to stdout/sterr
  bool cmd_streaming(const std::string &cmd) {
    LOG_DEBUG << "Running: " << cmd;
    return boost::process::system(cmd, boost::process::start_dir = root_) == 0;
  }

  bool fetch(const std::string &bundle_uri) {
    boost::filesystem::create_directories(root_);
    if (cmd_streaming(compose_ + "download " + bundle_uri)) {
      LOG_INFO << "Validating compose file";
      if (cmd_streaming(compose_ + "config")) {
        LOG_INFO << "Pulling containers";
        return cmd_streaming(compose_ + "pull");
      }
    }
    return false;
  };

  bool start() { return cmd_streaming(compose_ + "up --remove-orphans -d"); }

  void remove() {
    if (cmd_streaming(compose_ + "down")) {
      boost::filesystem::remove_all(root_);
    } else {
      LOG_ERROR << "docker-compose was unable to bring down: " << root_;
    }
  }

  std::string name_;
  boost::filesystem::path root_;
  std::string compose_;
};

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

bool ComposeBundleManager::fetchTarget(const Uptane::Target &target, Uptane::Fetcher &fetcher, const KeyManager &keys,
                                       FetcherProgressCb progress_cb, const api::FlowControlToken *token) {
  if (!OstreeManager::fetchTarget(target, fetcher, keys, progress_cb, token)) {
    return false;
  }

  LOG_INFO << "Looking for Compose Bundles to fetch";
  bool passed = true;
  for (const auto &pair : getBundles(target)) {
    LOG_INFO << "Fetching " << pair.first << " -> " << pair.second;
    if (!ComposeBundle(pair.first, cfg_).fetch(pair.second)) {
      passed = false;
    }
  }
  return passed;
}
