<h1 align="center">DeepStream 自定义 GStreamer 插件：udpmulticast_sink</h1>

> 一个用于在 NVIDIA DeepStream 推理管线尾部发送检测/识别结果到 UDP 组播的自定义 Sink 插件，并附带 EO 多目标协议（`eo_protocol_parser`）封装/解析示例与接收端工具。本文档提供插件概述 / 依赖 / 编译安装 / 使用方法 / 参数说明 / 组播接收示例 / 协议结构。

---

## 目录

1. 项目概述  
2. 功能特性  
3. 代码结构  
4. 依赖环境  
5. 构建与安装  
6. DeepStream 管线中使用示例  
7. 插件属性与配置  
8. EO 协议与数据说明  
9. 组播接收示例 (Python / C++)  
10. 常见问题 (FAQ)  
11. 后续工作建议  

---

## 1. 项目概述

`udpmulticast_sink` 是一个派生自 `GstBaseSink` 的 GStreamer 插件，设计用于 DeepStream 推理流程末端。它从 `GstBuffer` 中抓取 NvDsMeta（检测目标信息），对每帧内所有对象打包为 EO 多目标自定义二进制报文，通过 UDP 组播（Multicast）方式实时分发。

应用场景示例：
- 多路摄像头检测结果向边缘 / 融合服务器广播；
- 轻量分发给 Python、C++、嵌入式终端或其它服务进行告警、融合或二次分析；
- 自定义协议（含时间、设备、类别、置信度、目标像素统计等）。

---

## 2. 功能特性

- 支持 DeepStream 7.1（可通过 CMake 变量调整）。
- 支持 CUDA 12.x（默认 12.6 可覆盖）。
- 组播发送：可配置组播 IP 与端口 (`ip`, `port`)。
- 每帧多目标打包，包含：
  - 目标 ID / class_id / secondary classifier IDs；
  - 置信度、BBox、面积、像素统计（最小/平均像素）；
  - 时间戳、发送计数；
  - EO 协议头部字段（系统/子系统标识等拓展位）。
- C++ EO 协议打包 / 解析工具类：`EOProtocolParser`。
- 提供两种接收端：
  - Python：`recv_multicast.py`（快速调试）
  - C++：`receiver/eo_receiver`（协议级解析回调）
- 可选构建接收工具（`-DBUILD_EO_RECEIVER=ON/OFF`）。

---

## 3. 代码结构

```
gst-udpmulticast_sink/
  CMakeLists.txt                # 主插件 & 可选 receiver 构建
  gstudpmulticast_sink.cpp/.h   # GStreamer Sink 实现
  eo_protocol_parser.cpp/.h     # 协议封装/解析
  recv_multicast.py             # Python 组播接收 & 数据打印
  receiver/                     # C++ 组播接收 & 协议级解析
    CMakeLists.txt
    eo_receiver.cpp/.h
    main.cpp
  build/                        # (本地构建输出目录，可忽略入仓)
```

---

## 4. 依赖环境

运行/编译前请确认：

必需：
- Linux (x86_64 或 Jetson)；
- NVIDIA Driver 与 CUDA（默认使用 `/usr/local/cuda-<ver>`）；
- 已安装 DeepStream（默认路径 `/opt/nvidia/deepstream/deepstream-<NVDS_VERSION>`）；
- GStreamer 1.0 相关开发包（随 DeepStream 提供）；
- CMake >= 3.16；
- 编译器支持 C++14。

可选：
- `jsoncpp`（协议解析，如未找到会尝试直接链接 `jsoncpp` 名称）。

### 自定义可覆盖 CMake 变量
| 变量 | 说明 | 默认 |
|------|------|------|
| `CUDA_VER` | CUDA 版本目录 | `12.6` |
| `NVDS_VERSION` | DeepStream 主版本 (7.1 等) | `7.1` |
| `LIB_INSTALL_DIR` | DeepStream 库安装目录 | `/opt/nvidia/deepstream/deepstream-${NVDS_VERSION}/lib` |
| `GST_INSTALL_DIR` | GStreamer 插件安装目录 | `/opt/nvidia/deepstream/deepstream-${NVDS_VERSION}/lib/gst-plugins/` |
| `BUILD_EO_RECEIVER` | 是否构建 C++ 接收器 | `ON` |

---

## 5. 构建与安装

> 如需安装到 DeepStream 插件目录，通常需要 `sudo`。

```bash
# 进入源码目录
cd gst-udpmulticast_sink

# 创建并进入构建目录
cmake -B build -S . \
  -DCUDA_VER=12.6 \
  -DNVDS_VERSION=7.1 \
  -DBUILD_EO_RECEIVER=ON

cmake --build build -j$(nproc)

# （可选）安装：将 .so 放入 DeepStream 指定目录
sudo cmake --install build

# 验证插件是否已被 GStreamer 发现
gst-inspect-1.0 udpmulticast_sink | grep "DsUdpMulticastSink" || echo "Not Found"
```

构建后核心产物：
- `build/libudpmulticast_sink.so`（安装后位于 `${GST_INSTALL_DIR}`）
- （可选）`build/receiver/eo_receiver` C++ 接收端

---

## 6. DeepStream 管线中使用示例

### 6.1 gst-launch 简单拼接（示意）
```bash
gst-launch-1.0 filesrc location=sample.h264 ! h264parse ! nvv4l2decoder ! nvstreammux name=mux batch-size=1 width=1920 height=1080 ! nvinfer config-file-path=primary.txt ! nvtracker ! nvvideoconvert ! udpmulticast_sink ip=239.255.255.250 port=5000
```

> 实际 DeepStream 应用通常通过 `deepstream-app` 或自定义 C/C++ 管线集成，该插件可作为最终的 sink（可与其它 tee 并行）。

### 6.2 deepstream-app 自定义配置
在 `deepstream-app` 的 `sink` 部分可使用 `type=6` (如果采用自编程方式) 或通过添加自定义 bin；或者直接在自定义 C++ 代码中 `gst_element_factory_make("udpmulticast_sink", ...)`。

示例 C 代码片段：
```c
GstElement *mcast = gst_element_factory_make("udpmulticast_sink", "mcast_out");
g_object_set(mcast, "ip", "239.255.255.250", "port", 5000, NULL);
```

---

## 7. 插件属性与配置

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `ip` | string | `239.255.255.250` | 组播目的地址（D 类多播：224.0.0.0 ~ 239.255.255.255，建议使用 239.x 范围内部域） |
| `port` | uint (1~65535) | `5000` | 组播目的端口 |
| `silent` | boolean | `TRUE` | 预留（当前未启用详细日志控制） |

内部运行逻辑：
1. 在 `start()` 中初始化 GPU 设备（`cudaSetDevice`）。
2. 在 `render()` 中遍历 NvDsBatchMeta 中每帧和每个对象：
   - 收集目标 BBox / class_id / secondary classifier；
   - 统计最小像素、平均像素、分类计数；
   - 组装 `EOTargetInfo` 列表；
   - 使用 `EOProtocolParser::PackEOTargetMessage()` 打包；
   - 通过 UDP 组播 `sendto()` 发送。
3. 日志打印帧统计（`GST_INFO`）。

---

## 8. EO 协议与数据说明（简要）

协议头部 (`MessageHeader`) 采用紧凑结构（`#pragma pack(1)`），包含：
- `messageID` / `messageLength` / `sendCount` / `msgType`；
- 发送/接收端站号 + 系统类型（`SystemType`）编码；
- 时间字段（年/月/日/时/分/秒/子秒）；
- 子系统标识；
- 备用字段与报文体统计；
- 报文体类型（JSON/BINARY）及长度。

目标体 (`EOTargetInfo`) 关键字段：
- 时间；
- 设备类型 (`DeviceType`)；
- 目标 ID / 状态 / 跟踪模式；
- 角度 / 位姿（部分当前留空或默认值）；
- BBox 中心偏移、面积（`targetRect`）；
- 目标类别 (`TargetClass`)；
- 置信度 / 距离（距离可选未填）。

打包函数：
```cpp
std::vector<uint8_t> EOProtocolParser::PackEOTargetMessage(
    const std::vector<EOTargetInfo>& targets,
    uint16_t sendCount,
    uint8_t senderStation, SystemType senderSystem,
    uint8_t receiverStation, SystemType receiverSystem,
    uint8_t senderSubsystem, uint8_t receiverSubsystem);
```

接收端通过 `ParseEOTargetMessage()` 解析。

---

## 9. 组播接收示例

### 9.1 Python（快速调试）
脚本：`recv_multicast.py`（结构体解析版本，适配最初简单结构，可根据协议升级重写）。

```bash
python3 recv_multicast.py --group 239.255.255.250 --port 5000
```

常用参数：
| 参数 | 默认 | 说明 |
|------|------|------|
| `--group` | 239.255.10.10 | 组播地址（需与插件一致） |
| `--port` | 6000 | 端口（需与插件一致） |
| `--iface` | 0.0.0.0 | 本地网卡 IP（空则系统默认） |
| `--hex` | False | 打印十六进制原始数据 |
| `--quiet` | False | 精简输出 |

### 9.2 C++（协议解析）
构建开启 `BUILD_EO_RECEIVER=ON` 后生成：`receiver/eo_receiver`。

运行：
```bash
./build/receiver/eo_receiver 239.255.255.250 5000
```

回调中打印：`SendCount / Targets / 每个目标 (ID / Class / Conf / Rect ...)`。

---

## 10. 常见问题（FAQ）

1. 插件 gst-inspect 找不到？
   - 确认已执行 `sudo cmake --install build`；
   - 确认安装路径在 `GST_PLUGIN_PATH` 或 DeepStream 默认插件目录；
   - 运行：`gst-inspect-1.0 udpmulticast_sink` 查看是否报依赖错误。
2. 组播接收不到数据？
   - 确认发送与接收在同一二层网络且未被路由/防火墙阻断；
   - 检查组播地址是否为合法多播 (224.0.0.0/4)，避免使用 224.0.0.x 本地保留地址；
   - 尝试设置 `sysctl -w net.ipv4.conf.all.rp_filter=0`（某些网络需关闭严格反射）；
   - 在同机调试可开启环回：代码已设置 `IP_MULTICAST_LOOP=1`。 
3. 想发送单播/广播？
   - 可在代码中将 `inet_addr(self->ip)` 替换为目标 IP，并选择普通 UDP 地址（非多播范围）。
4. 想扩展报文内容？
   - 在 `EOTargetInfo` 中添加字段，并在打包/解析函数中同步修改；
   - 注意保持对齐（已使用紧凑结构打包）。

---

## 11. 后续可拓展建议

- 增加插件属性：TTL、网卡 (outgoing interface)、是否发送空帧、是否启用 JSON 附加信息；
- 支持可靠传输（如添加重传计数或基于 QUIC）；
- 增强安全性（可选签名或简单校验强化）；
- 增加 Prometheus 监控指标导出；
- 与 Kafka / MQTT 网关桥接；
- Python 端提供协议级解析版本（对齐 EO 协议头）。

---

## 作者

ShenChangli  (karmueo@163.com)  
欢迎 Issue / PR 交流改进。

---

> 文档版本：v1.0（根据源码自动整理）。如发现与实现不符，请以源码为准并提交更新建议。

---

返回顶部 ↑
