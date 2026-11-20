#include "network.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>

namespace dm {

std::vector<NetInterface> list_interfaces() {
    std::map<std::string, NetInterface> m;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return {};
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_name) continue;
        NetInterface& n = m[a->ifa_name];
        n.name = a->ifa_name;
        n.up = a->ifa_flags & IFF_UP; n.running = a->ifa_flags & IFF_RUNNING;
        for (const char* p : {"usb", "ncm", "rndis", "gadget", "enx"}) if (n.name.rfind(p, 0) == 0) n.usb_like = true;
        if (!a->ifa_addr) continue;
        char buf[INET6_ADDRSTRLEN];
        if (a->ifa_addr->sa_family == AF_INET) {
            auto* s = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
            if (::inet_ntop(AF_INET, &s->sin_addr, buf, sizeof buf)) n.ipv4.push_back(buf);
        } else if (a->ifa_addr->sa_family == AF_INET6) {
            auto* s = reinterpret_cast<sockaddr_in6*>(a->ifa_addr);
            if (::inet_ntop(AF_INET6, &s->sin6_addr, buf, sizeof buf)) n.ipv6.push_back(buf);
        }
    }
    ::freeifaddrs(list);
    std::vector<NetInterface> out;
    for (auto& [k, v] : m) out.push_back(v);
    return out;
}

json network_status_json(const std::string& bind_host, int port, bool tls) {
    json ifs = json::array();
    std::vector<std::string> urls;
    for (const auto& i : list_interfaces()) {
        ifs.push_back(json{{"name", i.name}, {"ipv4", i.ipv4}, {"ipv6", i.ipv6}, {"up", i.up}, {"running", i.running}, {"usb_like", i.usb_like}});
        if (i.name == "lo") continue;
        for (const auto& ip : i.ipv4) urls.push_back(std::string(tls ? "https://" : "http://") + ip + ":" + std::to_string(port) + "/");
    }
    return json{{"interfaces", ifs}, {"bind_host", bind_host}, {"port", port}, {"tls", tls}, {"urls", urls},
                {"note", "USB networking (NCM) is provided by the stock az0x-usb-gadget service; the address is read at runtime, not assumed."}};
}

} // namespace dm
