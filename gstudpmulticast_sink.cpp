/**
 * SECTION:element-_udpmulticast_sink
 *
 * FIXME:Describe _udpmulticast_sink here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! _udpmulticast_sink ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/gstinfo.h>
// #include "nvdsmeta.h"
#include "eo_protocol_parser.h"
#include "gstnvdsmeta.h"
#include "gstudpmulticast_sink.h"
#include "nvbufsurface.h"
#include <gst/base/gstbasetransform.h>
#include <gst/gstelement.h>
#include <gst/gstinfo.h>

#include "cuda_runtime_api.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <math.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>

/* enable to write transformed cvmat to files */
/* #define DSEXAMPLE_DEBUG */
/* 启用将转换后的 cvmat 写入文件 */
/* #define DSEXAMPLE_DEBUG */
static GQuark _dsmeta_quark = 0;

GST_DEBUG_CATEGORY_STATIC(gst_udpmulticast_sink_debug);
#define GST_CAT_DEFAULT gst_udpmulticast_sink_debug

#define CHECK_CUDA_STATUS(cuda_status, error_str)                              \
    do                                                                         \
    {                                                                          \
        if ((cuda_status) != cudaSuccess)                                      \
        {                                                                      \
            g_print("Error: %s in %s at line %d (%s)\n", error_str, __FILE__,  \
                    __LINE__, cudaGetErrorName(cuda_status));                  \
            goto error;                                                        \
        }                                                                      \
    } while (0)

/* Filter signals and args */
enum
{
    /* FILL ME */
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_SILENT,
    PROP_IP,
    PROP_PORT,
    PROP_IFACE,
    PROP_FPS
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

static void gst_udpmulticast_sink_set_property(GObject      *object,
                                               guint         property_id,
                                               const GValue *value,
                                               GParamSpec   *pspec);
static void gst_udpmulticast_sink_get_property(GObject    *object,
                                               guint       property_id,
                                               GValue     *value,
                                               GParamSpec *pspec);
static void gst_udpmulticast_sink_finalize(GObject *object);

#define gst_udpmulticast_sink_parent_class parent_class
G_DEFINE_TYPE(Gstudpmulticast_sink, gst_udpmulticast_sink, GST_TYPE_BASE_SINK);

static gboolean gst_udpmulticast_sink_set_caps(GstBaseSink *sink,
                                               GstCaps     *caps);

static GstFlowReturn gst_udpmulticast_sink_render(GstBaseSink *sink,
                                                  GstBuffer   *buf);
static gboolean      gst_udpmulticast_sink_start(GstBaseSink *sink);
static gboolean      gst_udpmulticast_sink_stop(GstBaseSink *sink);

static gdouble
get_current_time_seconds(void)
{
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return tv_now.tv_sec + tv_now.tv_usec / 1000000.0;
}

static gboolean
should_send_for_source(Gstudpmulticast_sink *self, guint source_id,
                       gdouble current_time)
{
    gdouble send_interval = (self->fps > 0) ? (1.0 / self->fps) : 0.04;
    auto    last_it = self->last_send_time_by_source.find(source_id);

    if (last_it == self->last_send_time_by_source.end() ||
        current_time - last_it->second >= send_interval)
    {
        self->last_send_time_by_source[source_id] = current_time;
        return TRUE;
    }

    return FALSE;
}

static void
fill_target_timestamp(EOTargetInfo *target_info)
{
    struct timeval tv;
    struct tm      tm_info;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_info);

    target_info->yr = tm_info.tm_year + 1900;
    target_info->mo = tm_info.tm_mon + 1;
    target_info->dy = tm_info.tm_mday;
    target_info->h = tm_info.tm_hour;
    target_info->min = tm_info.tm_min;
    target_info->sec = tm_info.tm_sec;
    target_info->msec = tv.tv_usec / 1000.0f;
}

static EOTargetInfo
create_empty_target(guint source_id)
{
    EOTargetInfo empty_target = {};

    fill_target_timestamp(&empty_target);

    empty_target.dev_id = 0;
    empty_target.guid_id = 0;
    empty_target.tar_id = 0;
    empty_target.trk_stat = 0;
    empty_target.trk_mod = 0;
    empty_target.fov_angle = 0.0;
    empty_target.lon = 0.0;
    empty_target.lat = 0.0;
    empty_target.alt = 0.0;
    empty_target.tar_a = 0.0;
    empty_target.tar_e = 0.0;
    empty_target.tar_rng = 0.0;
    empty_target.tar_av = 0.0;
    empty_target.tar_ev = 0.0;
    empty_target.tar_rv = 0.0;
    empty_target.fov_h = 0.0;
    empty_target.fov_v = 0.0;
    empty_target.offset_h = 0;
    empty_target.offset_v = 0;
    empty_target.tar_rect = 0;
    empty_target.tar_category = static_cast<int>(TargetClass::UNKNOWN);
    empty_target.tar_iden = "none";
    empty_target.tar_cfid = 0.0f;
    empty_target.source_id = source_id;

    return empty_target;
}

static void
log_detect_analysis(guint source_id, const DetectAnalysis &detect_analysis)
{
    GST_INFO("<===================================");
    GST_INFO("source_id: %u", source_id);
    GST_INFO("frameNum: %lu", detect_analysis.frameNum);
    for (std::map<guint16, guint>::const_iterator it =
             detect_analysis.primaryClassCountMap.begin();
         it != detect_analysis.primaryClassCountMap.end(); ++it)
    {
        GST_INFO("primaryClassCountMap: %d, %d", it->first, it->second);
    }
    for (std::map<guint16, guint>::const_iterator it =
             detect_analysis.secondaryClassCountMap.begin();
         it != detect_analysis.secondaryClassCountMap.end(); ++it)
    {
        GST_INFO("secondaryClassCountMap: %d, %d", it->first, it->second);
    }
    GST_INFO("minPixel: %d", detect_analysis.minPixel);
    GST_INFO("meanPixel: %d", detect_analysis.meanPixel);
    GST_INFO("===================================>");
}

/* GObject vmethod implementations */

/* initialize the _udpmulticast_sink's class */
static void gst_udpmulticast_sink_class_init(Gstudpmulticast_sinkClass *klass)
{
    GObjectClass     *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass  *gstelement_class;
    GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS(klass);

    gstelement_class = (GstElementClass *)klass;

    gobject_class->set_property = gst_udpmulticast_sink_set_property;
    gobject_class->get_property = gst_udpmulticast_sink_get_property;
    gobject_class->finalize = gst_udpmulticast_sink_finalize;

    base_sink_class->render = GST_DEBUG_FUNCPTR(gst_udpmulticast_sink_render);
    base_sink_class->start = GST_DEBUG_FUNCPTR(gst_udpmulticast_sink_start);
    base_sink_class->stop = GST_DEBUG_FUNCPTR(gst_udpmulticast_sink_stop);
    base_sink_class->set_caps =
        GST_DEBUG_FUNCPTR(gst_udpmulticast_sink_set_caps);

    gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass),
                                              &sink_factory);

    /* Set metadata describing the element */
    gst_element_class_set_details_simple(
        gstelement_class, "DsUdpMulticastSink plugin",
        "DsUdpMulticastSink Plugin",
        "Process a infer mst network on objects / full frame",
        "ShenChangli "
        "@ karmueo@163.com");

    /* install properties */
    g_object_class_install_property(
        gobject_class, PROP_SILENT,
        g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
                             TRUE, G_PARAM_READWRITE));
    g_object_class_install_property(
        gobject_class, PROP_IP,
        g_param_spec_string(
            "ip", "Multicast IP", "Multicast destination IP", "239.255.255.250",
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_PORT,
        g_param_spec_uint(
            "port", "Multicast Port", "Multicast destination port", 1, 65535,
            5000, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_IFACE,
        g_param_spec_string(
            "iface", "Network Interface",
            "Network interface name for multicast (e.g., eth0, enp5s0)", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_FPS,
        g_param_spec_uint(
            "fps", "Report FPS", "Frame rate for sending target reports", 1, 120,
            25, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 * 初始化新element
 * 实例化 pads 并将它们添加到element中
 * 设置 pad 回调函数
 * 初始化实例结构
 */
static void gst_udpmulticast_sink_init(Gstudpmulticast_sink *self)
{
    // 初始化一些参数
    self->gpu_id = 0;
    // default values
    self->ip = g_strdup("239.255.255.250");
    self->port = 5000;
    self->iface = NULL;
    self->fps = 25;
    self->send_count = 0;

    // 创建UDP Socket
    self->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (self->sockfd < 0)
    {
        GST_ERROR("Failed to create socket");
        return;
    }

    // 设置为非阻塞，防止网络异常时阻塞流线程导致上游误判为断流
    int flags = fcntl(self->sockfd, F_GETFL, 0);
    if (flags != -1)
    {
        if (fcntl(self->sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
            GST_WARNING("Failed to set multicast socket non-blocking: %s", strerror(errno));
    }
    else
    {
        GST_WARNING("Failed to query multicast socket flags: %s", strerror(errno));
    }

    // 设置组播地址
    memset(&self->multicast_addr, 0, sizeof(self->multicast_addr));
    self->multicast_addr.sin_family = AF_INET;
    self->multicast_addr.sin_addr.s_addr = inet_addr(self->ip);
    self->multicast_addr.sin_port = htons(self->port);

    // 设置TTL（可选）
    int ttl = 32;
    if (setsockopt(self->sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                   sizeof(ttl)) < 0)
    {
        GST_WARNING("Failed to set multicast TTL");
    }

    /* This quark is required to identify NvDsMeta when iterating through
     * the buffer metadatas */
    if (!_dsmeta_quark)
        _dsmeta_quark = g_quark_from_static_string(NVDS_META_STRING);
}

/**
 * 当元素从上游元素接收到输入缓冲区时调用。
 */
static GstFlowReturn gst_udpmulticast_sink_render(GstBaseSink *sink,
                                                  GstBuffer   *buf)
{
    Gstudpmulticast_sink *self = GST_UDPMULTICAST_SINK(sink);
    NvDsBatchMeta        *batch_meta = NULL;
    NvDsMetaList         *l_frame = NULL;
    GstMapInfo            in_map_info;
    gboolean              mapped = FALSE;

    memset(&in_map_info, 0, sizeof(in_map_info));
    if (!gst_buffer_map(buf, &in_map_info, GST_MAP_READ))
    {
        g_print("Error: Failed to map gst buffer\n");
        goto error;
    }
    mapped = TRUE;

    nvds_set_input_system_timestamp(buf, GST_ELEMENT_NAME(self));

    batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
    {
        GST_WARNING_OBJECT(self, "No batch meta on buffer, dropping payload");
        goto error;
    }
    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
         l_frame = l_frame->next)
    {
        NvDsFrameMeta            *frame_meta = (NvDsFrameMeta *)(l_frame->data);
        NvDsMetaList             *l_obj = NULL;
        std::vector<EOTargetInfo> target_infos;
        DetectAnalysis            detect_analysis = {};
        guint                     source_id = frame_meta->pad_index;  // 优先使用原始流索引，避免 tiled 后 source_id 被压成 0。
        guint                     total_object_count = 0;
        guint64                   total_pixel_sum = 0;
        gdouble                   current_time = get_current_time_seconds();
        gboolean                  should_send =
            should_send_for_source(self, source_id, current_time);

        detect_analysis.frameNum = frame_meta->frame_num + 1;
        detect_analysis.minPixel = G_MAXUINT16;

        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL;
             l_obj = l_obj->next)
        {
            NvDsObjectMeta *obj_meta = (NvDsObjectMeta *)(l_obj->data);

            if ((obj_meta->class_id >= 0))
            {
                std::map<guint16, guint>::iterator it =
                    detect_analysis.primaryClassCountMap.find(
                        obj_meta->class_id);
                if (it != detect_analysis.primaryClassCountMap.end())
                {
                    it->second++;
                }
                else
                {
                    detect_analysis.primaryClassCountMap.insert(
                        std::pair<guint16, guint>(obj_meta->class_id, 1));
                }

                guint16 final_class_id = obj_meta->class_id;
                float   final_confidence = obj_meta->confidence;
                gboolean has_classifier = FALSE;

                for (NvDsMetaList *l_class = obj_meta->classifier_meta_list;
                     l_class != NULL; l_class = l_class->next)
                {
                    NvDsClassifierMeta *cmeta =
                        (NvDsClassifierMeta *)l_class->data;
                    for (NvDsMetaList *l_label = cmeta->label_info_list;
                         l_label != NULL; l_label = l_label->next)
                    {
                        NvDsLabelInfo *label = (NvDsLabelInfo *)l_label->data;
                        auto it2 = detect_analysis.secondaryClassCountMap.find(
                            label->result_class_id);
                        if (it2 != detect_analysis.secondaryClassCountMap.end())
                            it2->second++;
                        else
                            detect_analysis.secondaryClassCountMap.insert(
                                std::pair<guint16, guint>(
                                    label->result_class_id, 1));

                        if (!has_classifier)
                        {
                            final_class_id = label->result_class_id;
                            final_confidence = label->result_prob;
                            has_classifier = TRUE;
                        }
                    }
                }

                guint32 pixel = (guint32)(obj_meta->rect_params.width *
                                          obj_meta->rect_params.height);
                if (pixel < detect_analysis.minPixel)
                {
                    detect_analysis.minPixel =
                        (guint16)MIN(pixel, (guint32)G_MAXUINT16);
                }
                total_pixel_sum += pixel;
                total_object_count++;

                EOTargetInfo targetInfo = {};
                fill_target_timestamp(&targetInfo);
                targetInfo.dev_id = 0;   // 固定为0（可见光）
                targetInfo.guid_id = 0;  // 固定为0
                targetInfo.tar_id = 0;   // 固定为0
                targetInfo.trk_stat = 1; // 默认正常，后续根据置信度调整
                targetInfo.trk_mod = 0;  // 固定为0（检测跟踪）

                targetInfo.fov_angle = 0.0;
                targetInfo.lon = 0.0;
                targetInfo.lat = 0.0;
                targetInfo.alt = 0.0;
                targetInfo.tar_a = 0.0;
                targetInfo.tar_e = 0.0;
                targetInfo.tar_rng = 0.0; // 没有距离信息填0
                targetInfo.tar_av = 0.0;
                targetInfo.tar_ev = 0.0;
                targetInfo.tar_rv = 0.0; // 没有距离信息填0
                targetInfo.fov_h = 0.0;
                targetInfo.fov_v = 0.0;

                targetInfo.offset_h = 0; // 固定为0
                targetInfo.offset_v = 0; // 固定为0
                targetInfo.tar_rect =
                    (int)(obj_meta->rect_params.left +
                          obj_meta->rect_params.width / 2); // 目标中心的像素值
                targetInfo.source_id = source_id;

                // 当前仅保留 UAV 一个类别，统一输出 tar_category=9 / tar_iden="uav"。
                targetInfo.tar_category = static_cast<int>(TargetClass::UAV);
                targetInfo.tar_iden = "uav";

                targetInfo.tar_cfid = final_confidence;
                targetInfo.trk_stat = (targetInfo.tar_cfid < 0.0f) ? 2 : 1;
                target_infos.push_back(targetInfo);
            }
        }

        if (total_object_count > 0)
        {
            detect_analysis.meanPixel =
                (guint16)MIN(total_pixel_sum / total_object_count,
                             (guint64)G_MAXUINT16);
        }
        else
        {
            detect_analysis.minPixel = 0;
            detect_analysis.meanPixel = 0;
        }

        if (target_infos.empty())
        {
            target_infos.push_back(create_empty_target(source_id));
        }

        if (should_send)
        {
            std::vector<uint8_t> message =
                EOProtocolParser::PackEOTargetMessage(target_infos,
                                                      ++self->send_count);

            if (!message.empty())
            {
                ssize_t sent = sendto(self->sockfd, message.data(), message.size(),
                                      MSG_DONTWAIT,
                                      (struct sockaddr *)&self->multicast_addr,
                                      sizeof(self->multicast_addr));
                if (sent < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        GST_WARNING_OBJECT(self, "Multicast socket busy, dropping frame");
                    }
                    else
                    {
                        GST_WARNING(
                            "Failed to send EO target message for source_id=%u with %zu targets: %s",
                            source_id, target_infos.size(), strerror(errno));
                    }
                }
                else
                {
                    GST_DEBUG("Successfully sent EO target message for source_id=%u "
                              "with %zu targets, size: %zu bytes (fps: %u)",
                              source_id, target_infos.size(), message.size(),
                              self->fps);
                }
            }
        }

        log_detect_analysis(source_id, detect_analysis);
    }

error:

    nvds_set_output_system_timestamp(buf, GST_ELEMENT_NAME(self));
    if (mapped)
        gst_buffer_unmap(buf, &in_map_info);
    return GST_FLOW_OK;
}

/**
 * 在元素从 ​READY​ 状态切换到 PLAYING/​PAUSED​ 状态时调用
 */
static gboolean gst_udpmulticast_sink_start(GstBaseSink *sink)
{
    g_print("gst_udpmulticast_sink_start\n");
    Gstudpmulticast_sink *self = GST_UDPMULTICAST_SINK(sink);

    self->last_send_time_by_source.clear();
    self->send_count = 0;

    CHECK_CUDA_STATUS(cudaSetDevice(self->gpu_id), "Unable to set cuda device");

    // 如果指定了网卡名称，绑定到该网卡
    if (self->iface && strlen(self->iface) > 0)
    {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, self->iface, IFNAMSIZ - 1);

        // 获取网卡索引
        if (ioctl(self->sockfd, SIOCGIFINDEX, &ifr) < 0)
        {
            GST_ERROR("Failed to get interface %s index: %s", self->iface,
                      strerror(errno));
            goto error;
        }

        // 绑定到指定网卡
        if (setsockopt(self->sockfd, SOL_SOCKET, SO_BINDTODEVICE, self->iface,
                       strlen(self->iface)) < 0)
        {
            GST_WARNING(
                "Failed to bind to interface %s: %s. Trying to continue...",
                self->iface, strerror(errno));
        }
        else
        {
            GST_INFO("Successfully bound to interface %s", self->iface);
        }

        // 设置组播发送接口
        struct in_addr local_interface;
        memset(&local_interface, 0, sizeof(local_interface));

        // 获取网卡IP地址
        if (ioctl(self->sockfd, SIOCGIFADDR, &ifr) < 0)
        {
            GST_WARNING("Failed to get interface %s address: %s", self->iface,
                        strerror(errno));
        }
        else
        {
            local_interface = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

            // 设置组播发送接口
            if (setsockopt(self->sockfd, IPPROTO_IP, IP_MULTICAST_IF,
                           &local_interface, sizeof(local_interface)) < 0)
            {
                GST_WARNING("Failed to set multicast interface: %s",
                            strerror(errno));
            }
            else
            {
                GST_INFO("Set multicast interface to %s (IP: %s)", self->iface,
                         inet_ntoa(local_interface));
            }
        }
    }

    return TRUE;
error:
    return FALSE;
}

/**
 * @brief 在元素从 PLAYING/​PAUSED 状态切换到 ​READY​​
 * 状态时调用
 *
 * @param trans 指向 GstBaseTransform 结构的指针。
 * @return 始终返回 TRUE。
 */
static gboolean gst_udpmulticast_sink_stop(GstBaseSink *sink)
{
    g_print("gst_udpmulticast_sink_stop\n");
    return TRUE;
}

/**
 * Called when source / sink pad capabilities have been negotiated.
 */
static gboolean gst_udpmulticast_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
    return TRUE;

error:
    return FALSE;
}

static void gst_udpmulticast_sink_set_property(GObject      *object,
                                               guint         property_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
    Gstudpmulticast_sink *self = GST_UDPMULTICAST_SINK(object);
    switch (property_id)
    {
    case PROP_SILENT:
        /* self->silent = g_value_get_boolean(value); */
        break;
    case PROP_IP:
        g_free(self->ip);
        self->ip = g_value_dup_string(value);
        if (self->ip)
            self->multicast_addr.sin_addr.s_addr = inet_addr(self->ip);
        break;
    case PROP_PORT:
        self->port = g_value_get_uint(value);
        self->multicast_addr.sin_port = htons(self->port);
        break;
    case PROP_IFACE:
        g_free(self->iface);
        self->iface = g_value_dup_string(value);
        break;
    case PROP_FPS:
        self->fps = g_value_get_uint(value);
        GST_INFO("Set report FPS to: %u", self->fps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void gst_udpmulticast_sink_get_property(GObject    *object,
                                               guint       property_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
    Gstudpmulticast_sink *self = GST_UDPMULTICAST_SINK(object);
    switch (property_id)
    {
    case PROP_SILENT:
        /* g_value_set_boolean(value, self->silent); */
        break;
    case PROP_IP:
        g_value_set_string(value, self->ip);
        break;
    case PROP_PORT:
        g_value_set_uint(value, self->port);
        break;
    case PROP_IFACE:
        g_value_set_string(value, self->iface);
        break;
    case PROP_FPS:
        g_value_set_uint(value, self->fps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void gst_udpmulticast_sink_finalize(GObject *object)
{
    Gstudpmulticast_sink *self = GST_UDPMULTICAST_SINK(object);
    if (self->sockfd >= 0)
    {
        close(self->sockfd);
        self->sockfd = -1;
    }
    g_clear_pointer(&self->ip, g_free);
    g_clear_pointer(&self->iface, g_free);
    GST_DEBUG_OBJECT(self, "finalize");
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean _udpmulticast_sink_init(GstPlugin *plugin)
{
    /* debug category for filtering log messages
     *
     * exchange the string 'Template _udpmulticast_sink' with your description
     */
    GST_DEBUG_CATEGORY_INIT(gst_udpmulticast_sink_debug, "udpmulticast_sink", 0,
                            "udpmulticast_sink plugin");

    return gst_element_register(plugin, "udpmulticast_sink", GST_RANK_PRIMARY,
                                GST_TYPE_UDPMULTICAST_SINK);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirst_udpmulticast_sink"
#endif

/* gstreamer looks for this structure to register _udpmulticast_sinks
 *
 * exchange the string 'Template udpmulticast_sink' with your _udpmulticast_sink
 * description
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  udpmulticast_sink,
                  DESCRIPTION,
                  _udpmulticast_sink_init,
                  "7.1",
                  LICENSE,
                  BINARY_PACKAGE,
                  URL)
