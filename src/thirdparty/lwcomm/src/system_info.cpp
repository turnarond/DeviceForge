/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: system_info.cpp .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "lwcomm/lwcomm.h"
#include <cctype>
#include <ctime>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#ifdef _WIN32

int LWSystemInfo::SysInfoIpList(std::list<std::string> &ip_list)
{
    ip_list.clear();
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddrs = NULL;
    ULONG ret;

    do {
        pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!pAddrs) return -1;
        ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
            NULL, pAddrs, &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            free(pAddrs);
            pAddrs = NULL;
        }
    } while (ret == ERROR_BUFFER_OVERFLOW);

    if (ret != NO_ERROR) {
        if (pAddrs) free(pAddrs);
        return -1;
    }

    for (PIP_ADAPTER_ADDRESSES pCurr = pAddrs; pCurr; pCurr = pCurr->Next) {
        if (pCurr->OperStatus != IfOperStatusUp) continue;
        for (PIP_ADAPTER_UNICAST_ADDRESS pAddr = pCurr->FirstUnicastAddress;
             pAddr; pAddr = pAddr->Next) {
            if (pAddr->Address.lpSockaddr->sa_family == AF_INET) {
                struct sockaddr_in *ipv4 =
                    (struct sockaddr_in *)pAddr->Address.lpSockaddr;
                char ipstr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ipv4->sin_addr, ipstr, sizeof(ipstr));
                if (strcmp(ipstr, "127.0.0.1") == 0) continue;
                ip_list.push_back(ipstr);
            }
        }
    }

    free(pAddrs);
    return LW_SUCCESS;
}

#else /* POSIX */

int LWSystemInfo::SysInfoIpList (std::list<std::string> &ip_list)
{
    ip_list.clear();
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)ifa->ifa_addr;
            char ipstr[INET_ADDRSTRLEN];

            inet_ntop(AF_INET, &ipv4->sin_addr, ipstr, sizeof(ipstr));
            if (strcmp(ipstr, "127.0.0.1") == 0) {
                continue;
            }
            if (strncmp(ifa->ifa_name, "docker", 6) == 0) {
                continue;
            }
            if (strncmp(ifa->ifa_name, "cn", 2) == 0) {
                continue;
            }
            if (strncmp(ifa->ifa_name, "veth", 4) == 0) {
                continue;
            }
            ip_list.push_back(ipstr);
        }
    }

    freeifaddrs(ifap);

    return LW_SUCCESS;
}

#endif /* _WIN32 */

bool LWSystemInfo::Ipv4Valid (std::string &ip)
{
    int count = 0;
    size_t start = 0;
    size_t end = ip.find('.');

    while (end != std::string::npos) {
        std::string part = ip.substr(start, end - start);
        if (part.empty() || !isdigit(part[0])) {
            return false;
        }
        int value = std::stoi(part);
        if (value < 0 || value > 255) {
            return false;
        }
        ++count;

        start = end + 1;
        end = ip.find('.', start);
    }

    /* Solve the last part of the IP address string. */
    std::string lastPart = ip.substr(start, ip.length() - start);
    if (!lastPart.empty() && isdigit(lastPart[0])) {
        int value = std::stoi(lastPart);
        if (value < 0 || value > 255) {
            return false;
        }
        ++count;
    }

    return (count == 4);
}

/*
 * end
 */
