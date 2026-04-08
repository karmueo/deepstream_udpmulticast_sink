#!/usr/bin/env python3
import argparse
import json
import socket
import struct
import sys
import time
from datetime import datetime

# 兼容旧版二进制报文的结构体布局。
# 当前仓库实际发送的是 JSON，因此这里仅作为回退解析使用。
STRUCT_FMT = '<ffffiQfQIii f'.replace(' ', '')
STRUCT_SIZE = struct.calcsize(STRUCT_FMT)

# 当前 app_config.yml 中 sink2 的默认组播参数。
DEFAULT_GROUP = '230.1.8.31'
DEFAULT_PORT = 8128
DEFAULT_IFACE = 'enP8p1s0'


def parse_args():
    """解析命令行参数。

    Returns:
        argparse.Namespace: 解析后的参数对象。
    """
    parser = argparse.ArgumentParser(
        description='接收并解析 DeepStream udpmulticast_sink 的检测报文。'
    )
    parser.add_argument(
        '--group',
        default=DEFAULT_GROUP,
        help='组播地址，默认与 app_config.yml 的 sink2.ip 保持一致。',
    )
    parser.add_argument(
        '--port',
        type=int,
        default=DEFAULT_PORT,
        help='组播端口，默认与 app_config.yml 的 sink2.multicast-port 保持一致。',
    )
    parser.add_argument(
        '--iface',
        default=DEFAULT_IFACE,
        help='本机接收网卡名或 IPv4 地址，默认与 app_config.yml 的 sink2.multicast-iface 保持一致。',
    )
    parser.add_argument(
        '--buffer-size',
        type=int,
        default=65535,
        help='单次接收缓冲区大小，默认 65535 字节。',
    )
    parser.add_argument(
        '--legacy-binary',
        action='store_true',
        help='强制按旧的二进制结构体格式解析，便于兼容历史报文。',
    )
    parser.add_argument(
        '--hex',
        action='store_true',
        help='打印原始负载的十六进制内容，便于排查报文格式问题。',
    )
    parser.add_argument(
        '--quiet',
        action='store_true',
        help='精简输出模式，每个报文只打印一行摘要。',
    )
    return parser.parse_args()


def resolve_iface_ip(iface: str) -> str:
    """将网卡名或 IPv4 地址解析为用于加入组播的 IPv4 地址。

    Args:
        iface: 网卡名、IPv4 地址或 `0.0.0.0`。

    Returns:
        解析后的 IPv4 地址。

    Raises:
        OSError: 当网卡名无法解析为 IPv4 地址时抛出。
    """
    if not iface or iface == '0.0.0.0':
        return '0.0.0.0'

    parts = iface.split('.')
    if len(parts) == 4 and all(part.isdigit() for part in parts):
        return iface

    try:
        import fcntl
    except ImportError as exc:
        raise OSError('当前平台不支持通过网卡名解析 IPv4 地址') from exc

    # 仅用于临时 ioctl 查询网卡 IPv4 地址，不参与实际收包。
    query_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        ifreq = struct.pack('256s', iface.encode('utf-8')[:15])
        response = fcntl.ioctl(query_sock.fileno(), 0x8915, ifreq)
        return socket.inet_ntoa(response[20:24])
    finally:
        query_sock.close()


def join_multicast(sock: socket.socket, group: str, iface_ip: str):
    """让套接字加入指定组播组。

    Args:
        sock: 已创建的 UDP 套接字。
        group: 组播地址。
        iface_ip: 本机用于加入组播的 IPv4 地址。
    """
    # 先设置地址复用，避免同机多个接收端抢占端口。
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except (AttributeError, OSError):
        pass

    # IP_ADD_MEMBERSHIP 需要 group + iface IPv4。
    membership = struct.pack('=4s4s', socket.inet_aton(group), socket.inet_aton(iface_ip))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)

    # 本机调试时允许组播回环。
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    except OSError:
        pass


def decode_json_packet(data: bytes):
    """将 JSON 组播报文解析为字典。

    Args:
        data: UDP 负载字节流。

    Returns:
        dict: 解析后的 JSON 对象。

    Raises:
        ValueError: 当负载不是合法 JSON 时抛出。
    """
    text = data.decode('utf-8', errors='strict').strip('\x00\r\n\t ')
    payload = json.loads(text)
    if not isinstance(payload, dict):
        raise ValueError('JSON 根节点不是对象')
    return payload


def decode_legacy_packet(data: bytes):
    """将旧版二进制结构体报文解析为字典。

    Args:
        data: UDP 负载字节流。

    Returns:
        dict: 解析后的结构体字段。

    Raises:
        ValueError: 当报文长度不足时抛出。
    """
    if len(data) < STRUCT_SIZE:
        raise ValueError(f'Packet too small ({len(data)} bytes), expected {STRUCT_SIZE}')

    (
        left,
        top,
        width,
        height,
        class_id,
        object_id,
        confidence,
        ntp_ts,
        source_id,
        detect_class_id,
        classify_class_id,
        classify_confidence,
    ) = struct.unpack_from(STRUCT_FMT, data)

    return {
        'bbox': {
            'left': left,
            'top': top,
            'width': width,
            'height': height,
            'area': width * height,
        },
        'class_id': class_id,
        'object_id': object_id,
        'confidence': confidence,
        'ntp_timestamp': ntp_ts,
        'source_id': source_id,
        'detect_class_id': detect_class_id,
        'classify_class_id': classify_class_id,
        'classify_confidence': classify_confidence,
        'raw_len': len(data),
    }


def format_ntp(ts: int):
    """尽量把时间戳格式化为可读时间。

    Args:
        ts: 原始时间戳。

    Returns:
        str: 格式化后的时间字符串。
    """
    if ts == 0:
        return '0'

    now = time.time()
    for scale, label in ((1.0, 's'), (1e3, 'ms'), (1e6, 'us'), (1e9, 'ns')):
        candidate = ts / scale
        if 946684800 <= candidate <= now + 86400:
            return datetime.fromtimestamp(candidate).isoformat() + f' (~{label})'
    return str(ts)


def summarize_json_target(target: dict) -> str:
    """生成单个 JSON 目标的摘要字符串。

    Args:
        target: `cont` 数组中的单个目标对象。

    Returns:
        str: 目标摘要。
    """
    return (
        f"source_id={target.get('source_id', 0)} "
        f"tar_iden={target.get('tar_iden', '')} "
        f"tar_category={target.get('tar_category', 0)} "
        f"trk_stat={target.get('trk_stat', 0)} "
        f"tar_cfid={float(target.get('tar_cfid', 0.0) or 0.0):.4f} "
        f"tar_rect={target.get('tar_rect', 0)}"
    )


def print_json_packet(payload: dict, addr, recv_time: float, hex_dump: bool, quiet: bool, raw_data: bytes):
    """打印 JSON 报文内容。

    Args:
        payload: 解析后的 JSON 对象。
        addr: 发送端地址。
        recv_time: 本地接收时间戳。
        hex_dump: 是否打印十六进制原始内容。
        quiet: 是否使用精简输出。
        raw_data: 原始 UDP 负载。
    """
    cont = payload.get('cont', [])  # 报文中的目标数组。
    if not isinstance(cont, list):
        cont = []  # 非法 cont 类型时降级为空列表。

    msg_id = int(payload.get('msg_id', 0) or 0)  # 报文 ID。
    msg_sn = int(payload.get('msg_sn', 0) or 0)  # 发送计数。
    cont_sum = int(payload.get('cont_sum', len(cont)) or 0)  # 目标数量字段。
    source_ids = sorted({target.get('source_id', 0) for target in cont}) if cont else []  # 本报文涉及的视频源编号。

    if quiet:
        source_id_text = ','.join(str(source_id) for source_id in source_ids) if source_ids else '-'  # 摘要中的源编号文本。
        first_target = summarize_json_target(cont[0]) if cont else 'no-target'  # 精简模式下展示第一个目标。
        print(
            f"ts={recv_time:.6f} src={addr[0]}:{addr[1]} "
            f"msg_id={msg_id} msg_sn={msg_sn} cont_sum={cont_sum} "
            f"targets={len(cont)} sources={source_id_text} {first_target}"
        )
        return

    print('-' * 80)
    print(f'Received from {addr[0]}:{addr[1]} bytes={len(raw_data)}')
    print(f' Local recv time: {datetime.fromtimestamp(recv_time).isoformat()}')
    print(f' msg_id: {msg_id}')
    print(f' msg_sn: {msg_sn}')
    print(f' msg_type: {payload.get("msg_type", 0)}')
    print(f' cont_type: {payload.get("cont_type", 0)}')
    print(f' cont_sum: {cont_sum}')
    print(f' actual_targets: {len(cont)}')
    if cont_sum != len(cont):
        print(f' [WARN] cont_sum 与实际目标数不一致: cont_sum={cont_sum}, actual={len(cont)}')

    print(
        ' Header time: '
        f'{int(payload.get("yr", 0) or 0):04d}-'
        f'{int(payload.get("mo", 0) or 0):02d}-'
        f'{int(payload.get("dy", 0) or 0):02d} '
        f'{int(payload.get("h", 0) or 0):02d}:'
        f'{int(payload.get("min", 0) or 0):02d}:'
        f'{int(payload.get("sec", 0) or 0):02d}.'
        f'{float(payload.get("msec", 0.0) or 0.0):.3f}'
    )

    for index, target in enumerate(cont, start=1):
        print(f'  Target[{index}]: {summarize_json_target(target)}')

    if hex_dump:
        print(' Raw Hex:', raw_data.hex())


def print_legacy_packet(decoded: dict, addr, recv_time: float, hex_dump: bool, quiet: bool, raw_data: bytes):
    """打印旧版二进制结构体报文内容。

    Args:
        decoded: 结构体解析后的字典。
        addr: 发送端地址。
        recv_time: 本地接收时间戳。
        hex_dump: 是否打印十六进制原始内容。
        quiet: 是否使用精简输出。
        raw_data: 原始 UDP 负载。
    """
    if quiet:
        print(
            f"ts={recv_time:.6f} src={addr[0]}:{addr[1]} "
            f"det={decoded['detect_class_id']} cls={decoded['classify_class_id']} "
            f"dconf={decoded['confidence']:.3f} cconf={decoded['classify_confidence']:.3f} "
            f"bbox=({decoded['bbox']['left']:.1f},{decoded['bbox']['top']:.1f},"
            f"{decoded['bbox']['width']:.1f},{decoded['bbox']['height']:.1f}) "
            f"src_id={decoded['source_id']}"
        )
        return

    print('-' * 80)
    print(f'Received from {addr[0]}:{addr[1]} bytes={decoded["raw_len"]}')
    print(f' Local recv time: {datetime.fromtimestamp(recv_time).isoformat()}')
    print(f" Detection Class ID (legacy/class_id): {decoded['class_id']}")
    print(f" Detect Class ID (explicit): {decoded['detect_class_id']}")
    print(f" Classification ID: {decoded['classify_class_id']}")
    print(f" Classification Confidence: {decoded['classify_confidence']:.4f}")
    print(f" Object ID: {decoded['object_id']}")
    print(f" Detection Confidence: {decoded['confidence']:.4f}")
    print(f" Source ID: {decoded['source_id']}")
    print(f" NTP Timestamp: {decoded['ntp_timestamp']} -> {format_ntp(decoded['ntp_timestamp'])}")
    bbox = decoded['bbox']
    print(
        f" BBox: left={bbox['left']:.1f} top={bbox['top']:.1f} "
        f"w={bbox['width']:.1f} h={bbox['height']:.1f} area={bbox['area']:.1f}"
    )

    if hex_dump:
        print(' Raw Hex:', raw_data[:STRUCT_SIZE].hex())


def main():
    """程序入口，接收组播报文并打印解析结果。"""
    args = parse_args()  # 命令行参数对象。
    iface_ip = resolve_iface_ip(args.iface)  # 用于加入组播的本机 IPv4 地址。

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)  # UDP 组播接收套接字。
    # 先绑定端口，再加入组播，避免某些平台对接口/端口顺序更敏感。
    try:
        sock.bind(('', args.port))
    except OSError as exc:
        print(f'Bind failed on port {args.port}: {exc}', file=sys.stderr)
        sys.exit(1)

    try:
        join_multicast(sock, args.group, iface_ip)
    except OSError as exc:
        print(
            f'加入组播失败: group={args.group}, iface={args.iface}, iface_ip={iface_ip}, error={exc}',
            file=sys.stderr,
        )
        sys.exit(1)

    print(
        f'Listening on multicast {args.group}:{args.port} '
        f'iface={args.iface} iface_ip={iface_ip} '
        f'buffer={args.buffer_size} legacy_binary={args.legacy_binary}'
    )

    while True:
        data, addr = sock.recvfrom(args.buffer_size)  # 本次收到的 UDP 负载和发送端地址。
        recv_time = time.time()  # 本地接收时间戳。

        if args.legacy_binary:
            try:
                decoded = decode_legacy_packet(data)  # 旧版二进制结构体解析结果。
            except Exception as exc:
                print(f'[WARN] {addr} len={len(data)} legacy decode error: {exc}')
                if args.hex:
                    print(data.hex())
                continue
            print_legacy_packet(decoded, addr, recv_time, args.hex, args.quiet, data)
            continue

        try:
            payload = decode_json_packet(data)
            print_json_packet(payload, addr, recv_time, args.hex, args.quiet, data)
        except Exception as json_error:
            # 兼容历史二进制报文，JSON 失败后再尝试旧格式。
            try:
                decoded = decode_legacy_packet(data)
            except Exception:
                print(f'[WARN] {addr} len={len(data)} json decode error: {json_error}')
                if args.hex:
                    print(data.hex())
                continue
            print_legacy_packet(decoded, addr, recv_time, args.hex, args.quiet, data)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nExit.')
