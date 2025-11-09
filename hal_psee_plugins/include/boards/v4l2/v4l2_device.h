/**********************************************************************************************************************
 * Copyright (c) Prophesee S.A.                                                                                       *
 *                                                                                                                    *
 * Licensed under the Apache License, Version 2.0 (the "License");                                                    *
 * you may not use this file except in compliance with the License.                                                   *
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0                                 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed   *
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.                      *
 * See the License for the specific language governing permissions and limitations under the License.                 *
 **********************************************************************************************************************/

#ifndef METAVISION_HAL_PSEE_PLUGINS_V4L2_DEVICE_H
#define METAVISION_HAL_PSEE_PLUGINS_V4L2_DEVICE_H

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <filesystem>
#include <linux/media.h>

#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

#include "metavision/hal/facilities/i_camera_synchronization.h"
#include "metavision/hal/utils/device_control.h"
#include "metavision/psee_hw_layer/boards/v4l2/v4l2_controls.h"
#include "metavision/psee_hw_layer/utils/psee_format.h"
#include "metavision/hal/utils/hal_connection_exception.h"

namespace Metavision {

using V4l2Capability = struct v4l2_capability;

struct media_entity {
    int fd;
    std::filesystem::path path;
    uint32_t type;
    struct media_entity_desc desc;
};

class V4L2DeviceControl : public DeviceControl {
    V4l2Capability cap_;
    enum v4l2_buf_type buf_type_;
    int media_fd_ = -1;
    std::vector<media_entity> entities_;
    std::shared_ptr<V4L2Controls> controls_;

public:
    /* Count the number of bytes received in the buffer. The complexity is log(n) */
    template<typename Data>
    static std::size_t nb_not_null_data(const Data *const buf_beg_addr, std::size_t length_in_bytes) {
        auto is_not_null = [](const auto &d) { return d != 0; };
        auto beg         = reinterpret_cast<const uint64_t *>(buf_beg_addr);
        auto end         = beg + length_in_bytes / sizeof(uint64_t);

        auto it_pp = std::partition_point(beg, end, is_not_null);
        return std::distance(beg, it_pp) * sizeof(*beg);
    }

    V4L2DeviceControl(const std::string &dev_name);
    virtual ~V4L2DeviceControl() = default;

    V4l2Capability get_capability() const;
    const struct media_entity *get_sensor_entity() const;
    const struct media_entity *get_video_entity() const;
    bool can_crop(int fd);
    void set_crop(int fd, const struct v4l2_rect &rect);
    void get_native_size(int fd, struct v4l2_rect &rect);
    void get_crop(int fd, struct v4l2_rect &rect);

    StreamFormat get_format() const;

    int enumerate_entities();
    std::shared_ptr<V4L2Controls> get_controls();

    // DeviceControl
public:
    virtual void start() override;
    virtual void stop() override;
    virtual void reset() override;
};

} // namespace Metavision

#endif // METAVISION_HAL_PSEE_PLUGINS_V4L2_DEVICE_H
