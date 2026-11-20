// USB-C network status: lists interfaces and addresses so the UI can show
// the actual device address instead of assuming 192.168.155.1.
#pragma once
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

struct NetInterface {
    std::string name;
    std::vector<std::string> ipv4, ipv6;
    bool up = false, running = false;
    bool usb_like = false;   // usb*, ncm*, rndis*, gadget*
};

std::vector<NetInterface> list_interfaces();
json network_status_json(const std::string& bind_host, int port, bool tls);

} // namespace dm
