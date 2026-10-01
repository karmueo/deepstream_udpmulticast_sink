#ifndef NORMALIZED_RECT_H
#define NORMALIZED_RECT_H

#include <algorithm>
#include <array>
#include <cmath>

// 将像素框裁剪到图像边界，再归一化为 [左上角x, 左上角y, 宽, 高]。
inline bool
normalize_target_rect(double left, double top, double width, double height,
                      double frame_width, double frame_height,
                      std::array<double, 4> &normalized_rect)
{
    normalized_rect = {};
    if (!std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(frame_width) || !std::isfinite(frame_height) ||
        width <= 0.0 || height <= 0.0 ||
        frame_width <= 0.0 || frame_height <= 0.0)
    {
        return false;
    }

    const double x = std::max(0.0, std::min(left, frame_width));
    const double y = std::max(0.0, std::min(top, frame_height));
    const double right = std::max(0.0, std::min(left + width, frame_width));
    const double bottom = std::max(0.0, std::min(top + height, frame_height));
    if (right <= x || bottom <= y)
    {
        return false;
    }

    normalized_rect = {x / frame_width, y / frame_height,
                       (right - x) / frame_width, (bottom - y) / frame_height};
    return true;
}

#endif // NORMALIZED_RECT_H
