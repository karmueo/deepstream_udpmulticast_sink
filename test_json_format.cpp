#include "eo_protocol_parser.h"
#include "normalized_rect.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <jsoncpp/json/json.h>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void require_rect(const std::array<double, 4> &actual,
                         const std::array<double, 4> &expected)
{
    for (size_t i = 0; i < expected.size(); ++i)
    {
        require(std::abs(actual[i] - expected[i]) < 1e-9,
                "Unexpected normalized rectangle");
    }
}

static void test_normalization()
{
    std::array<double, 4> rect;
    require(normalize_target_rect(160, 120, 320, 240, 640, 480, rect),
            "640x480 frame failed");
    require_rect(rect, {0.25, 0.25, 0.5, 0.5});
    require(normalize_target_rect(480, 270, 960, 540, 1920, 1080, rect),
            "1920x1080 frame failed");
    require_rect(rect, {0.25, 0.25, 0.5, 0.5});
    require(normalize_target_rect(1854, 270, 40, 108, 1920, 1080, rect),
            "Right-side target failed");
    require_rect(rect, {0.965625, 0.25, 1.0 / 48.0, 0.1});
    require(normalize_target_rect(-64, -48, 128, 96, 640, 480, rect),
            "Negative-origin clipping failed");
    require_rect(rect, {0.0, 0.0, 0.1, 0.1});
    require(normalize_target_rect(576, 432, 128, 96, 640, 480, rect),
            "Right/bottom clipping failed");
    require_rect(rect, {0.9, 0.9, 0.1, 0.1});
    require(normalize_target_rect(0, 0, 640, 480, 640, 480, rect),
            "Full-frame box failed");
    require_rect(rect, {0.0, 0.0, 1.0, 1.0});
    require(!normalize_target_rect(640, 0, 20, 20, 640, 480, rect),
            "Fully outside box accepted");
    require(!normalize_target_rect(0, 0, 20, 20, 0, 480, rect),
            "Zero frame width accepted");
    require(!normalize_target_rect(0, 0, -20, 20, 640, 480, rect),
            "Negative box width accepted");
    require(!normalize_target_rect(std::numeric_limits<double>::quiet_NaN(),
                                   0, 20, 20, 640, 480, rect),
            "Non-finite coordinate accepted");
}

static void test_json_contract(const std::string &json_text,
                               const std::vector<EOTargetInfo> &expected)
{
    Json::Value json;
    Json::Reader reader;
    require(reader.parse(json_text, json), "Packed message is invalid JSON");
    require(json["cont_sum"].asUInt() == expected.size(), "Wrong cont_sum");
    for (Json::ArrayIndex i = 0; i < expected.size(); ++i)
    {
        const Json::Value &rect = json["cont"][i]["tar_rect"];
        require(rect.isArray() && rect.size() == 4, "tar_rect must be an array of four numbers");
        for (Json::ArrayIndex j = 0; j < 4; ++j)
        {
            require(rect[j].isNumeric() &&
                    std::abs(rect[j].asDouble() - expected[i].tar_rect[j]) < 1e-9,
                    "Normalized coordinate lost during serialization");
        }
    }

    const char *invalid_rects[] = {
        "1874", "null", "[]", "[0,0,1]", "[0,0,1,1,0]",
        "[0,0,1,\"1\"]", "[0,0,true,1]", "[-0.1,0,0.2,0.3]", "[0,0,1.1,1]"
    };
    for (const char *invalid_rect : invalid_rects)
    {
        Json::Value invalid;
        require(reader.parse(invalid_rect, invalid), "Invalid test fixture");
        Json::Value bad_message = json;
        // 保留第一个有效目标，确认后续无效框不会被静默忽略或留下部分结果。
        bad_message["cont"][1]["tar_rect"] = invalid;
        Json::StreamWriterBuilder writer;
        const std::string payload = Json::writeString(writer, bad_message);
        MessageHeader header{};
        std::vector<EOTargetInfo> targets;
        require(!EOProtocolParser::ParseEOTargetMessage(
                    reinterpret_cast<const uint8_t *>(payload.data()),
                    payload.size(), header, targets),
                "Malformed tar_rect accepted");
        require(targets.empty(), "Partial targets returned after a parse failure");
    }
}

int main() {
    test_normalization();
    // 创建测试目标信息
    std::vector<EOTargetInfo> targetInfos;
    
    // 目标1
    EOTargetInfo target1{};
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
    target1.tar_rect = {0.25, 0.25, 0.5, 0.5};
    target1.source_id = 3;
    targetInfos.push_back(target1);
    
    // 目标2，示例中使用行人类别，验证不同 tar_category / tar_iden 可以并存。
    EOTargetInfo target2 = target1;
    target2.tar_category = static_cast<int>(TargetClass::PEDESTRIAN);
    target2.tar_iden = "人";
    target2.tar_cfid = 0.88f;
    target2.tar_rect = {0.965625, 0.25, 1.0 / 48.0, 0.1};
    targetInfos.push_back(target2);

    EOTargetInfo empty_target{};
    empty_target.tar_iden = "none";
    targetInfos.push_back(empty_target);
    
    // 打包消息
    uint16_t sendCount = 1;
    std::vector<uint8_t> message = EOProtocolParser::PackEOTargetMessage(targetInfos, sendCount);
    
    if (message.empty()) {
        std::cerr << "Failed to pack message!" << std::endl;
        return 1;
    }
    
    // 输出JSON字符串（假设是纯JSON，没有二进制头部）
    std::string jsonStr(message.begin(), message.end());
    test_json_contract(jsonStr, targetInfos);
    std::cout << "Generated JSON Message:" << std::endl;
    std::cout << jsonStr << std::endl;
    std::cout << "\nMessage size: " << message.size() << " bytes" << std::endl;
    
    // 测试解析
    MessageHeader header;
    std::vector<EOTargetInfo> parsedTargets;
    if (EOProtocolParser::ParseEOTargetMessage(message.data(), message.size(), header, parsedTargets)) {
        require(parsedTargets.size() == targetInfos.size(), "Targets lost during parsing");
        for (size_t i = 0; i < parsedTargets.size(); ++i)
        {
            require_rect(parsedTargets[i].tar_rect, targetInfos[i].tar_rect);
            require(parsedTargets[i].source_id == targetInfos[i].source_id,
                    "source_id lost during parsing");
            require(parsedTargets[i].tar_iden == targetInfos[i].tar_iden,
                    "Target label lost during parsing");
        }
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
            const auto &rect = parsedTargets[i].tar_rect;
            std::cout << "  tar_rect: [" << rect[0] << "," << rect[1] << ","
                      << rect[2] << "," << rect[3] << "]" << std::endl;
            std::cout << "  source_id: " << parsedTargets[i].source_id << std::endl;
        }
    } else {
        std::cerr << "Failed to parse message!" << std::endl;
        return 1;
    }
    
    return 0;
}
