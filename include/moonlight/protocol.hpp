#pragma once
#include <local-state/state.hpp>

#include <moonlight/data-structures.hpp>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

constexpr auto M_VERSION = "7.1.431.0";
constexpr auto M_GFE_VERSION = "3.23.0.74";

/**
 * @brief Step 1: GET server informations
 *
 * @param local_state
 * @param isServerBusy
 * @param current_appid
 * @param display_modes
 * @param clientID
 * @param local_endpoint_address
 * @return pt::ptree
 */
pt::ptree serverinfo(LocalState &local_state,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> display_modes,
                     const std::string clientID,
                     const std::string local_endpoint_address);