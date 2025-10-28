#include "eo_protocol_parser.h"
#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <memory>
#include <sstream>
#include <sys/time.h>

std::vector<uint8_t>
EOProtocolParser::PackEOTargetMessage(const std::vector<EOTargetInfo> &targetInfos,
                                      uint16_t                          sendCount)
{
    // 如果没有目标信息，返回空消息
    if (targetInfos.empty()) {
        return std::vector<uint8_t>();
    }

    // 创建报文头
    MessageHeader header;
    FillMessageHeader(header, sendCount, static_cast<int>(targetInfos.size()));

    // 创建JSON报文 - 包含报文头和目标数组
    Json::Value jsonMessage;
    
    // 添加报文头
    jsonMessage["msg_id"] = header.msg_id;
    jsonMessage["msg_sn"] = header.msg_sn;
    jsonMessage["msg_type"] = header.msg_type;
    jsonMessage["tx_sys_id"] = header.tx_sys_id;
    jsonMessage["tx_dev_type"] = header.tx_dev_type;
    jsonMessage["tx_dev_id"] = header.tx_dev_id;
    jsonMessage["tx_subdev_id"] = header.tx_subdev_id;
    jsonMessage["rx_sys_id"] = header.rx_sys_id;
    jsonMessage["rx_dev_type"] = header.rx_dev_type;
    jsonMessage["rx_dev_id"] = header.rx_dev_id;
    jsonMessage["rx_subdev_id"] = header.rx_subdev_id;
    jsonMessage["yr"] = header.yr;
    jsonMessage["mo"] = header.mo;
    jsonMessage["dy"] = header.dy;
    jsonMessage["h"] = header.h;
    jsonMessage["min"] = header.min;
    jsonMessage["sec"] = header.sec;
    jsonMessage["msec"] = header.msec;
    jsonMessage["cont_type"] = header.cont_type;
    jsonMessage["cont_sum"] = header.cont_sum;
    
    // 添加目标数组
    jsonMessage["cont"] = Json::Value(Json::arrayValue);
    for (const auto& targetInfo : targetInfos) {
        Json::Value targetJson = CreateTargetInfoJson(targetInfo);
        jsonMessage["cont"].append(targetJson);
    }
    
    auto        writerBuilder = GetWriterBuilder();
    std::string jsonStr = Json::writeString(*writerBuilder, jsonMessage);

    // 将JSON字符串转换为字节数组
    std::vector<uint8_t> message(jsonStr.begin(), jsonStr.end());

    return message;
}

bool EOProtocolParser::ParseEOTargetMessage(const uint8_t           *data,
                                            size_t                   length,
                                            MessageHeader           &header,
                                            std::vector<EOTargetInfo> &targetInfos)
{
    if (length < 10) // 至少需要一些数据
    {
        return false;
    }

    // 解析JSON报文
    const char *jsonStr = reinterpret_cast<const char *>(data);
    auto        readerBuilder = GetReaderBuilder();
    Json::Value jsonMessage;
    std::string errors;

    std::istringstream jsonStream(std::string(jsonStr, length));
    if (!Json::parseFromStream(*readerBuilder, jsonStream, &jsonMessage, &errors))
    {
        return false;
    }

    // 解析报文头
    try {
        header.msg_id = jsonMessage["msg_id"].asInt();
        header.msg_sn = jsonMessage["msg_sn"].asInt();
        header.msg_type = jsonMessage["msg_type"].asInt();
        header.tx_sys_id = jsonMessage["tx_sys_id"].asInt();
        header.tx_dev_type = jsonMessage["tx_dev_type"].asInt();
        header.tx_dev_id = jsonMessage["tx_dev_id"].asInt();
        header.tx_subdev_id = jsonMessage["tx_subdev_id"].asInt();
        header.rx_sys_id = jsonMessage["rx_sys_id"].asInt();
        header.rx_dev_type = jsonMessage["rx_dev_type"].asInt();
        header.rx_dev_id = jsonMessage["rx_dev_id"].asInt();
        header.rx_subdev_id = jsonMessage["rx_subdev_id"].asInt();
        header.yr = jsonMessage["yr"].asInt();
        header.mo = jsonMessage["mo"].asInt();
        header.dy = jsonMessage["dy"].asInt();
        header.h = jsonMessage["h"].asInt();
        header.min = jsonMessage["min"].asInt();
        header.sec = jsonMessage["sec"].asInt();
        header.msec = jsonMessage["msec"].asFloat();
        header.cont_type = jsonMessage["cont_type"].asInt();
        header.cont_sum = jsonMessage["cont_sum"].asInt();
    } catch (...) {
        return false;
    }

    // 清空目标列表
    targetInfos.clear();

    // 解析目标数组
    if (jsonMessage.isMember("cont") && jsonMessage["cont"].isArray()) {
        const Json::Value& contArray = jsonMessage["cont"];
        for (const auto& targetJson : contArray) {
            EOTargetInfo targetInfo;
            if (ParseTargetInfoFromJson(targetJson, targetInfo)) {
                targetInfos.push_back(targetInfo);
            }
        }
    }

    return !targetInfos.empty();
}

uint16_t EOProtocolParser::CalculateChecksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i += 2)
    {
        if (i + 1 < length)
        {
            sum += (data[i] << 8) | data[i + 1];
        }
        else
        {
            sum += data[i] << 8;
        }
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}

bool EOProtocolParser::VerifyFrameTail(const uint8_t *data, size_t length)
{
    // 保留用于兼容，新JSON格式不使用帧尾
    return true;
}

void EOProtocolParser::FillMessageHeader(MessageHeader &header,
                                         int            msg_sn,
                                         int            cont_sum)
{
    // 获取当前时间
    std::time_t now = std::time(nullptr);
    struct tm  *tm = std::localtime(&now);
    
    // 获取毫秒
    struct timeval tv;
    gettimeofday(&tv, NULL);
    float msec = tv.tv_usec / 1000.0f;

    // 填充报文头 - 按照需求设置固定值
    header.msg_id = 0x7112;      // 固定为0x7112
    header.msg_sn = msg_sn;      // 报文计数
    header.msg_type = 3;         // 固定为3（数据流）
    header.tx_sys_id = 0;        // 固定为0
    header.tx_dev_type = 1;      // 固定为1（光电）
    header.tx_dev_id = 0;        // 固定为0
    header.tx_subdev_id = 0;     // 固定为0
    header.rx_sys_id = 0;        // 固定为0
    header.rx_dev_type = 1;      // 固定为1
    header.rx_dev_id = 0;        // 固定为0
    header.rx_subdev_id = 0;     // 固定为0
    header.yr = tm->tm_year + 1900;  // 年
    header.mo = tm->tm_mon + 1;      // 月
    header.dy = tm->tm_mday;         // 日
    header.h = tm->tm_hour;          // 时
    header.min = tm->tm_min;         // 分
    header.sec = tm->tm_sec;         // 秒
    header.msec = msec;              // 毫秒
    header.cont_type = 1;        // 固定为1（多信息）
    header.cont_sum = cont_sum;  // 目标数量
}

bool EOProtocolParser::ParseTargetInfoFromJson(const Json::Value &json,
                                               EOTargetInfo      &targetInfo)
{
    try
    {
        targetInfo.yr = json["yr"].asInt();
        targetInfo.mo = json["mo"].asInt();
        targetInfo.dy = json["dy"].asInt();
        targetInfo.h = json["h"].asInt();
        targetInfo.min = json["min"].asInt();
        targetInfo.sec = json["sec"].asInt();
        targetInfo.msec = json["msec"].asFloat();
        targetInfo.dev_id = json["dev_id"].asInt();
        targetInfo.guid_id = json["guid_id"].asInt();
        targetInfo.tar_id = json["tar_id"].asInt();
        targetInfo.trk_stat = json["trk_stat"].asInt();
        targetInfo.trk_mod = json["trk_mod"].asInt();
        targetInfo.fov_angle = json["fov_angle"].asDouble();
        targetInfo.lon = json["lon"].asDouble();
        targetInfo.lat = json["lat"].asDouble();
        targetInfo.alt = json["alt"].asDouble();
        targetInfo.tar_a = json["tar_a"].asDouble();
        targetInfo.tar_e = json["tar_e"].asDouble();
        targetInfo.tar_rng = json["tar_rng"].asDouble();
        targetInfo.tar_av = json["tar_av"].asDouble();
        targetInfo.tar_ev = json["tar_ev"].asDouble();
        targetInfo.tar_rv = json["tar_rv"].asDouble();
        targetInfo.tar_category = json["tar_category"].asInt();
        targetInfo.tar_iden = json["tar_iden"].asString();
        targetInfo.tar_cfid = json["tar_cfid"].asFloat();
        targetInfo.fov_h = json["fov_h"].asDouble();
        targetInfo.fov_v = json["fov_v"].asDouble();
        targetInfo.offset_h = json["offset_h"].asInt();
        targetInfo.offset_v = json["offset_v"].asInt();
        targetInfo.tar_rect = json["tar_rect"].asInt();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

Json::Value
EOProtocolParser::CreateTargetInfoJson(const EOTargetInfo &targetInfo)
{
    Json::Value json;
    json["yr"] = targetInfo.yr;
    json["mo"] = targetInfo.mo;
    json["dy"] = targetInfo.dy;
    json["h"] = targetInfo.h;
    json["min"] = targetInfo.min;
    json["sec"] = targetInfo.sec;
    json["msec"] = targetInfo.msec;
    json["dev_id"] = targetInfo.dev_id;
    json["guid_id"] = targetInfo.guid_id;
    json["tar_id"] = targetInfo.tar_id;
    json["trk_stat"] = targetInfo.trk_stat;
    json["trk_mod"] = targetInfo.trk_mod;
    json["fov_angle"] = targetInfo.fov_angle;
    json["lon"] = targetInfo.lon;
    json["lat"] = targetInfo.lat;
    json["alt"] = targetInfo.alt;
    json["tar_a"] = targetInfo.tar_a;
    json["tar_e"] = targetInfo.tar_e;
    json["tar_rng"] = targetInfo.tar_rng;
    json["tar_av"] = targetInfo.tar_av;
    json["tar_ev"] = targetInfo.tar_ev;
    json["tar_rv"] = targetInfo.tar_rv;
    json["tar_category"] = targetInfo.tar_category;
    json["tar_iden"] = targetInfo.tar_iden;
    json["tar_cfid"] = targetInfo.tar_cfid;
    json["fov_h"] = targetInfo.fov_h;
    json["fov_v"] = targetInfo.fov_v;
    json["offset_h"] = targetInfo.offset_h;
    json["offset_v"] = targetInfo.offset_v;
    json["tar_rect"] = targetInfo.tar_rect;

    return json;
}

std::unique_ptr<Json::StreamWriterBuilder> EOProtocolParser::GetWriterBuilder()
{
    auto builder = std::make_unique<Json::StreamWriterBuilder>();
    builder->settings_["indentation"] = ""; // 紧凑格式，无缩进
    return builder;
}

std::unique_ptr<Json::CharReaderBuilder> EOProtocolParser::GetReaderBuilder()
{
    return std::make_unique<Json::CharReaderBuilder>();
}