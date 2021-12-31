#pragma once
#include <helpers/state.hpp>

#include <moonlight/data-structures.hpp>
#include <moonlight/user-pair.hpp>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

constexpr auto M_VERSION = "7.1.431.0";
constexpr auto M_GFE_VERSION = "3.23.0.74";

/**
 * @brief Step 1: GET server informations
 * 
 * @param local_state 
 * @param pair_handler 
 * @param isServerBusy 
 * @param current_appid 
 * @param display_modes 
 * @param clientID 
 * @return pt::ptree 
 */
pt::ptree serverinfo(LocalState &local_state,
                     UserPair &pair_handler,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> display_modes,
                     const std::string clientID);