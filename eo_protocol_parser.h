#ifndef EOPROTOCOLPARSER_H
#define EOPROTOCOLPARSER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 前向声明 Json 相关类
namespace Json
{
class Value;
class StreamWriter;
class StreamWriterBuilder;
class CharReader;
class CharReaderBuilder;
} // namespace Json

// 系统类型定义
enum class SystemType : uint8_t
{
    S_BAND = 0,   // 0-S
    KUX_BAND = 1, // 1-Ku/X
    EO = 2,       // 2-光电
    FUSION = 3,   // 3-融合
    UAV = 4       // 4-无人机
};

// 消息类型定义
enum class MessageType : uint16_t
{
    CONTROL = 0,    // 0:控制
    FEEDBACK = 1,   // 1:回馈
    QUERY = 2,      // 2:查询
    DATA_STREAM = 3 // 3:数据流
};

// 报文类型定义
enum class BodyType : uint16_t
{
    JSON = 0,  // 0:json
    BINARY = 1 // 1:二进制
};

// 报文ID定义
enum class MessageID : uint16_t
{
    SYSTEM_PARAM_QUERY = 0x7101,       // 系统参数查询(指令)
    SYSTEM_PARAM = 0x7102,             // 系统参数
    BIT_STATUS_QUERY = 0x7101,         // 比特状态查询(指令)
    BIT_STATUS = 0x7103,               // 比特状态
    GUIDANCE_INFO = 0x7104,            // 引导信息
    TARGET_INFO = 0x7105,              // 目标信息 0x7105
    TRACK_CONTROL = 0x7106,            // 跟踪控制
    TRACK_CONTROL_FEEDBACK = 0x7106,   // 跟踪控制反馈
    SERVO_CONTROL = 0x7107,            // 伺服控制
    SERVO_CONTROL_FEEDBACK = 0x7107,   // 伺服控制反馈
    VISIBLE_LIGHT_CONTROL = 0x7108,    // 可见光控制
    VISIBLE_LIGHT_CONTROL_FB = 0x7108, // 可见光控制反馈
    INFRARED_CONTROL = 0x7109,         // 红外控制
    INFRARED_CONTROL_FB = 0x7109,      // 红外控制反馈
    RANGING = 0x7110,                  // 测距
    RANGING_FEEDBACK = 0x7110          // 测距反馈
};

// 目标状态定义
enum class TargetStatus : int
{
    NORMAL = 1,      // 1正常
    LOST = 0,        // 0丢失
    EXTRAPOLATED = 2 // 2外推
};

// 跟踪模式定义
enum class TrackMode : int
{
    DETECTION_TRACK = 0,  // 0检测跟踪
    RECOGNITION_TRACK = 1 // 1识别跟踪
};

// 目标类型定义
enum class TargetClass : int
{
    UNKNOWN = 0,         // 0:不明
    BIRD_FLOCK = 1,      // 1:鸟群
    AIRBORNE_OBJECT = 2, // 2:空飘物
    AIRCRAFT = 3,        // 3:飞机
    CAR = 4,             // 4:汽车
    BIG_BIRD = 5,        // 5:大鸟
    SMALL_BIRD = 6,      // 6:小鸟
    PEDESTRIAN = 7,      // 7:行人
    CRUISE_MISSILE = 8,  // 8:巡航导弹
    UAV = 9,             // 9:无人机
    UNKNOWN_15 = 15      // 15:未知
};

// 设备类型定义
enum class DeviceType : int
{
    VISIBLE_LIGHT = 0,  // 0可见光
    THERMAL_IMAGING = 1 // 1热成像
};

// 报文头结构体（JSON格式）
struct MessageHeader
{
    int   msg_id;       // 报文ID, 唯一标识（整型），固定为0x7112
    int   msg_sn;       // 报文计数（整型）
    int   msg_type;     // 报文类型, 0：控制；1：回馈；2：查询；3数据流（备份）（整型），固定为3
    int   tx_sys_id;    // 系统号（整型），固定为0
    int   tx_dev_type;  // 0-雷达 1-光电 2-侦收 3-融合 4-无人机（反制）5-ads-b 6-ais 7-干扰反制（整型），固定为1
    int   tx_dev_id;    // 每个设备编号（根据系统要求编），固定为0
    int   tx_subdev_id; // 参见雷达分系统编号（光电0-可见光，1-红外，2-测距；侦收  0-定向 1-定位），固定为0
    int   rx_sys_id;    // 系统号（整型），999-不指定，固定为0
    int   rx_dev_type;  // 0-雷达 1-光电 2-侦收 3-融合 4-无人机（反制）5-ads-b 6-ais 7-干扰反制, 999-不指定（整型），固定为1
    int   rx_dev_id;    // 每个设备编号（根据系统要求编），999-不指定，固定为0
    int   rx_subdev_id; // 参见雷达分系统编号（光电0-可见光，1-红外，2-测距；侦收  0-定向 1-定位），999-不指定，固定为0
    int   yr;           // 年（整型）
    int   mo;           // 月（整型）
    int   dy;           // 日（整型）
    int   h;            // 时（整型）
    int   min;          // 分（整型）
    int   sec;          // 秒（整型）
    float msec;         // 毫秒（单精度浮点）
    int   cont_type;    // 信息类型，0单信息，1多信息，固定为1
    int   cont_sum;     // 目标数量
};

// 光电目标信息结构体
struct EOTargetInfo
{
    int          yr;            // 年（整型）
    int          mo;            // 月（整型）
    int          dy;            // 日（整型）
    int          h;             // 时（整型）
    int          min;           // 分（整型）
    int          sec;           // 秒（整型）
    float        msec;          // 毫秒（单精度浮点）
    int          dev_id;        // 设备类型，0可见光，1热成像（整型），固定为0
    int          guid_id;       // 引导批号（整型），自主跟踪0，固定为0
    int          tar_id;        // 目标批号（整型），固定为0
    int          trk_stat;      // 目标状态，1正常，0丢失，2外推（整型），固定为1
    int          trk_mod;       // 0检测跟踪，1识别跟踪（整型），固定为0
    double       fov_angle;     // 视场（双精度浮点），固定为0
    double       lon;           // 站址经度（精度≤1e-7）（双精度浮点），0
    double       lat;           // 站址纬度（精度≤1e-7）（双精度浮点），0
    double       alt;           // 站址海拔高度，单位米（精度≤1e-2）（双精度浮点），0
    double       tar_a;         // 目标水平角，度（双精度浮点），0
    double       tar_e;         // 目标垂直角，度（双精度浮点），0
    double       tar_rng;       // 目标距离，单位米，没有距离信息填0（双精度浮点）
    double       tar_av;        // 目标水平角速度，度（双精度浮点），0
    double       tar_ev;        // 目标垂直角速度，度（双精度浮点），0
    double       tar_rv;        // 目标径向速度，单位米/s，没有距离信息填0（双精度浮点）
    int          tar_category;  // 目标类型（整型）- 继续保持TargetClass
    std::string  tar_iden;      // 目标具体型号（字符串），bird或者uav
    float        tar_cfid;      // 目标置信度（单精度浮点）
    double       fov_h;         // 视场中心水平角度，单位度（双精度浮点）0
    double       fov_v;         // 视场中心垂直角度（双精度浮点）0
    int          offset_h;      // 水平脱靶量（整型）0
    int          offset_v;      // 垂直脱靶量（整型）0
    int          tar_rect;      // 目标位置，元素（整型），目标中心的像素值
};

// 光电报文封装和解析类
class EOProtocolParser
{
  public:
    // 封装光电目标信息报文（多目标）- 新JSON格式
    static std::vector<uint8_t>
    PackEOTargetMessage(const std::vector<EOTargetInfo> &targetInfos,
                        uint16_t                          sendCount);

    // 解析光电目标信息报文（多目标）- 新JSON格式
    static bool ParseEOTargetMessage(const uint8_t           *data,
                                     size_t                   length,
                                     MessageHeader           &header,
                                     std::vector<EOTargetInfo> &targetInfos);

    // 计算校验和（保留用于兼容）
    static uint16_t CalculateChecksum(const uint8_t *data, size_t length);

    // 验证帧尾（保留用于兼容）
    static bool VerifyFrameTail(const uint8_t *data, size_t length);

  private:
    // 填充报文头
    static void FillMessageHeader(MessageHeader &header,
                                  int            msg_sn,
                                  int            cont_sum);

    // 从JSON解析目标信息
    static bool ParseTargetInfoFromJson(const Json::Value &json,
                                        EOTargetInfo      &targetInfo);

    // 生成目标信息的JSON
    static Json::Value CreateTargetInfoJson(const EOTargetInfo &targetInfo);

    // 生成报文头的JSON
    static Json::Value CreateMessageHeaderJson(const MessageHeader &header);

    // JSON 写入器构建器
    static std::unique_ptr<Json::StreamWriterBuilder> GetWriterBuilder();

    // JSON 读取器构建器
    static std::unique_ptr<Json::CharReaderBuilder> GetReaderBuilder();
};

#endif // EOPROTOCOLPARSER_H