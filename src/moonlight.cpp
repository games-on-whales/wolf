#include <moonlight/protocol.hpp>

namespace pt = boost::property_tree;

pt::ptree serverinfo(LocalState &local_state,
                     UserPair &pair_handler,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> display_modes,
                     const std::string clientID) {
  int pair_status = pair_handler.isPaired(clientID);
  pt::ptree tree;

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.hostname", local_state.hostname());

  tree.put("root.appversion", M_VERSION);
  tree.put("root.GfeVersion", M_GFE_VERSION);
  tree.put("root.uniqueid", local_state.get_uuid());

  tree.put("root.MaxLumaPixelsHEVC",
           "0"); // TODO: tree.put("root.MaxLumaPixelsHEVC",config::video.hevc_mode > 1 ? "1869449984" : "0");
  tree.put("root.ServerCodecModeSupport", "3"); // TODO: what are the modes here?

  tree.put("root.HttpsPort", local_state.map_port(local_state.HTTPS_PORT));
  tree.put("root.ExternalPort", local_state.map_port(local_state.HTTP_PORT));
  tree.put("root.mac", local_state.mac_address());
  tree.put("root.ExternalIP", local_state.external_ip());
  tree.put("root.LocalIP", local_state.local_ip());

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