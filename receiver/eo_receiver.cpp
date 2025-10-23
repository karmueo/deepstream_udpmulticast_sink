#include "eo_receiver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <net/if.h>
#include <sys/ioctl.h>

// 辅助函数：通过网卡名称获取IP地址
static bool getInterfaceIP(const std::string& ifname, std::string& ipAddr) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        return false;
    }

    close(fd);
    
    struct sockaddr_in* addr = (struct sockaddr_in*)&ifr.ifr_addr;
    ipAddr = inet_ntoa(addr->sin_addr);
    return true;
}

EOReceiver::EOReceiver(const std::string& mcastIp, uint16_t port, const std::string& localIf)
    : mcastIp_(mcastIp), port_(port), localIf_(localIf) {}

EOReceiver::~EOReceiver() { stop(); }

bool EOReceiver::start() {
    if (running_) return true;

    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        std::cerr << "EOReceiver: socket create failed: " << strerror(errno) << std::endl;
        return false;
    }

    int reuse = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "EOReceiver: setsockopt SO_REUSEADDR failed: " << strerror(errno) << std::endl;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "EOReceiver: bind failed: " << strerror(errno) << std::endl;
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(mcastIp_.c_str());
    if (!localIf_.empty()) {
        // 尝试判断是网卡名称还是IP地址
        struct in_addr addr;
        if (inet_aton(localIf_.c_str(), &addr) != 0) {
            // 输入是有效的IP地址
            mreq.imr_interface.s_addr = addr.s_addr;
            std::cout << "EOReceiver: Binding to interface IP: " << localIf_ << std::endl;
        } else {
            // 输入可能是网卡名称，尝试获取其IP
            std::string ifIP;
            if (getInterfaceIP(localIf_, ifIP)) {
                mreq.imr_interface.s_addr = inet_addr(ifIP.c_str());
                std::cout << "EOReceiver: Binding to interface " << localIf_ 
                          << " (IP: " << ifIP << ")" << std::endl;
            } else {
                std::cerr << "EOReceiver: Failed to get IP for interface: " << localIf_ 
                          << ", using INADDR_ANY" << std::endl;
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            }
        }
    } else {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    }

    if (setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "EOReceiver: join multicast failed: " << strerror(errno) << std::endl;
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    running_ = true;
    th_ = std::thread(&EOReceiver::recvLoop, this);
    return true;
}

void EOReceiver::stop() {
    if (!running_) return;
    running_ = false;
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
    }
    if (th_.joinable()) th_.join();
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

void EOReceiver::recvLoop() {
    constexpr size_t BUF_SIZE = 64 * 1024; // 足够容纳当前 JSON 报文
    std::vector<uint8_t> buf(BUF_SIZE);

    while (running_) {
        ssize_t n = ::recv(sockfd_, buf.data(), buf.size(), 0);
        if (n <= 0) {
            if (!running_) break;
            if (n < 0 && (errno == EINTR)) continue;
            if (n < 0) {
                std::cerr << "EOReceiver: recv error: " << strerror(errno) << std::endl;
            }
            continue;
        }

        MessageHeader header{};
        std::vector<EOTargetInfo> targets;
        if (EOProtocolParser::ParseEOTargetMessage(buf.data(), static_cast<size_t>(n), header, targets)) {
            if (callback_) {
                callback_(header, targets);
            } else {
                std::cout << "Received EO Target Message: count=" << targets.size() << std::endl;
                for (const auto& t : targets) {
                    std::cout << "  ID=" << t.targetID
                              << " class=" << static_cast<int>(t.targetClass)
                              << " conf=" << t.targetConfidence
                              << " offset(h,v)=" << t.offsetHorizontal << "," << t.offsetVertical
                              << " rect=" << t.targetRect << std::endl;
                }
            }
        } else {
            std::cerr << "EOReceiver: parse failed (size=" << n << ")" << std::endl;
        }
    }
}
