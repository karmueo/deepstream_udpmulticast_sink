#include "eo_receiver.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

static std::atomic<bool> g_stop{false};

static void handleSig(int){ g_stop = true; }

int main(int argc, char** argv) {
    std::string ip = "239.255.10.10";
    uint16_t port = 6000;
    std::string bind_if = "eno2";  // 绑定网卡名
    
    if (argc > 1) ip = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc > 3) bind_if = argv[3];  // 第三个参数可以是网卡名称(如eno2)或IP地址

    std::cout << "EO Receiver listen multicast " << ip << ":" << port;
    if (!bind_if.empty()) {
        std::cout << " (bind to interface: " << bind_if << ")";
    }
    std::cout << std::endl;

    EOReceiver receiver(ip, port, bind_if);
    receiver.setCallback([](const MessageHeader& header, const std::vector<EOTargetInfo>& targets){
        std::cout << "---- Parsed Message ----" << std::endl;
        std::cout << "SendCount=" << header.sendCount << " Targets=" << targets.size() << std::endl;
        for (const auto& t : targets) {
            std::cout << "ID=" << t.targetID
                      << " Class=" << static_cast<int>(t.targetClass)
                      << " Conf=" << t.targetConfidence
                      << " Offset=" << t.offsetHorizontal << "," << t.offsetVertical
                      << " Rect=" << t.targetRect
                      << std::endl;
        }
    });

    if (!receiver.start()) {
        std::cerr << "Failed to start receiver" << std::endl;
        return 1;
    }

    std::signal(SIGINT, handleSig);
    std::signal(SIGTERM, handleSig);

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    receiver.stop();
    std::cout << "Receiver stopped" << std::endl;
    return 0;
}
