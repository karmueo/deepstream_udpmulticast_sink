#include "eo_protocol_parser.h"
#include <iostream>
#include <iomanip>

int main() {
    // 创建测试目标信息
    std::vector<EOTargetInfo> targetInfos;
    
    // 目标1
    EOTargetInfo target1;
    target1.yr = 2025;
    target1.mo = 10;
    target1.dy = 28;
    target1.h = 14;
    target1.min = 30;
    target1.sec = 45;
    target1.msec = 123.456f;
    target1.dev_id = 0;        // 可见光
    target1.guid_id = 0;
    target1.tar_id = 0;
    target1.trk_stat = 1;      // 正常
    target1.trk_mod = 0;       // 检测跟踪
    target1.fov_angle = 0.0;
    target1.lon = 0.0;
    target1.lat = 0.0;
    target1.alt = 0.0;
    target1.tar_a = 0.0;
    target1.tar_e = 0.0;
    target1.tar_rng = 0.0;
    target1.tar_av = 0.0;
    target1.tar_ev = 0.0;
    target1.tar_rv = 0.0;
    target1.tar_category = static_cast<int>(TargetClass::UAV);
    target1.tar_iden = "uav";
    target1.tar_cfid = 0.95f;
    target1.fov_h = 0.0;
    target1.fov_v = 0.0;
    target1.offset_h = 0;
    target1.offset_v = 0;
    target1.tar_rect = 1024;
    targetInfos.push_back(target1);
    
    // 目标2
    EOTargetInfo target2 = target1;
    target2.tar_category = static_cast<int>(TargetClass::SMALL_BIRD);
    target2.tar_iden = "bird";
    target2.tar_cfid = 0.88f;
    target2.tar_rect = 512;
    targetInfos.push_back(target2);
    
    // 打包消息
    uint16_t sendCount = 1;
    std::vector<uint8_t> message = EOProtocolParser::PackEOTargetMessage(targetInfos, sendCount);
    
    if (message.empty()) {
        std::cerr << "Failed to pack message!" << std::endl;
        return 1;
    }
    
    // 输出JSON字符串（假设是纯JSON，没有二进制头部）
    std::string jsonStr(message.begin(), message.end());
    std::cout << "Generated JSON Message:" << std::endl;
    std::cout << jsonStr << std::endl;
    std::cout << "\nMessage size: " << message.size() << " bytes" << std::endl;
    
    // 测试解析
    MessageHeader header;
    std::vector<EOTargetInfo> parsedTargets;
    if (EOProtocolParser::ParseEOTargetMessage(message.data(), message.size(), header, parsedTargets)) {
        std::cout << "\nParsing successful!" << std::endl;
        std::cout << "Header info:" << std::endl;
        std::cout << "  msg_id: 0x" << std::hex << header.msg_id << std::dec << std::endl;
        std::cout << "  msg_sn: " << header.msg_sn << std::endl;
        std::cout << "  msg_type: " << header.msg_type << std::endl;
        std::cout << "  cont_sum: " << header.cont_sum << std::endl;
        std::cout << "  tx_dev_type: " << header.tx_dev_type << std::endl;
        std::cout << "\nParsed " << parsedTargets.size() << " targets:" << std::endl;
        for (size_t i = 0; i < parsedTargets.size(); ++i) {
            std::cout << "Target " << (i+1) << ":" << std::endl;
            std::cout << "  tar_category: " << parsedTargets[i].tar_category << std::endl;
            std::cout << "  tar_iden: " << parsedTargets[i].tar_iden << std::endl;
            std::cout << "  tar_cfid: " << parsedTargets[i].tar_cfid << std::endl;
            std::cout << "  tar_rect: " << parsedTargets[i].tar_rect << std::endl;
        }
    } else {
        std::cerr << "Failed to parse message!" << std::endl;
        return 1;
    }
    
    return 0;
}
