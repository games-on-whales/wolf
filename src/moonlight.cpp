#include <moonlight/protocol.hpp>

namespace pt = boost::property_tree;

namespace moonlight {

pt::ptree serverinfo(const Config &config,
                     const UserPair &pair_handler,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> display_modes,
                     const std::string clientID) {
  int pair_status = pair_handler.isPaired(clientID);
  pt::ptree tree;

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.hostname", config.hostname());

  tree.put("root.appversion", M_VERSION);
  tree.put("root.GfeVersion", M_GFE_VERSION);
  tree.put("root.uniqueid", config.get_uuid());

  tree.put("root.MaxLumaPixelsHEVC", "0");
  // TODO: tree.put("root.MaxLumaPixelsHEVC",config::video.hevc_mode > 1 ? "1869449984" : "0");
  tree.put("root.ServerCodecModeSupport", "3"); // TODO: what are the modes here?

  tree.put("root.HttpsPort", config.map_port(config.HTTPS_PORT));
  tree.put("root.ExternalPort", config.map_port(config.HTTP_PORT));
  tree.put("root.mac", config.mac_address());
  tree.put("root.ExternalIP", config.external_ip());
  tree.put("root.LocalIP", config.local_ip());

  pt::ptree display_nodes;
  for (auto mode : display_modes) {
    pt::ptree display_node;
    display_node.put("Width", mode.width);
    display_node.put("Height", mode.height);
    display_node.put("RefreshRate", mode.refreshRate);

    display_nodes.add_child("DisplayMode", display_node);
  }

  tree.add_child("root.SupportedDisplayMode", display_nodes);
  tree.put("root.PairStatus", pair_status);
  tree.put("root.currentgame", current_appid);
  tree.put("root.state", isServerBusy ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");
  return tree;
}
} // namespace moonlight