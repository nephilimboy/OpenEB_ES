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

#include <cstdint>
#include <cstring>
#include <filesystem>
#include "boards/v4l2/v4l2_device.h"

// Linux specific headers
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "metavision/psee_hw_layer/boards/v4l2/v4l2_controls.h"

using namespace Metavision;

V4L2DeviceControl::V4L2DeviceControl(const std::string &devpath) {
    struct stat st;
    if (-1 == stat(devpath.c_str(), &st)) {
        throw HalConnectionException(errno, std::generic_category(), devpath + "Cannot identify device.");
    }

    if (!S_ISCHR(st.st_mode)) {
        throw HalConnectionException(ENODEV, std::generic_category(), devpath + " is not a device");
    }

    media_fd_ = open(devpath.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (-1 == media_fd_) {
        throw HalConnectionException(errno, std::generic_category(), devpath + "Cannot open media device");
    }

    enumerate_entities();

    auto video_ent = get_video_entity();

    if (ioctl(video_ent->fd, VIDIOC_QUERYCAP, &cap_)) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_QUERYCAP failed");
    }

    if (cap_.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if (cap_.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        throw HalConnectionException(ENOTSUP, std::generic_category(), devpath + " is not video capture device");
    }

    if (!(cap_.capabilities & V4L2_CAP_STREAMING)) {
        throw HalConnectionException(ENOTSUP, std::generic_category(), devpath + " does not support streaming i/o");
    }

    // only expose sensor controls for now
    controls_ = std::make_shared<V4L2Controls>(get_sensor_entity()->fd);
    // Note: this code expects the V4L2 device to be configured to output a supported format
}

StreamFormat V4L2DeviceControl::get_format() const {
    uint32_t width, height;
    struct v4l2_format fmt = {.type = buf_type_};
    struct v4l2_subdev_selection crop_bound {
        .which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = 0, .target = V4L2_SEL_TGT_CROP_BOUNDS,
    };

    if (ioctl(get_video_entity()->fd, VIDIOC_G_FMT, &fmt) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_G_FMT failed");
    }

    /* v4l2_pix_format_mplane and v4l2_pix_format both start with
     * width, height, pixelformat
     * thus we can disregard the actual fmt.type
     */
    if (ioctl(get_sensor_entity()->fd, VIDIOC_SUBDEV_G_SELECTION, &crop_bound) < 0) {
        MV_HAL_LOG_TRACE() << "Could not get CROP_BOUND selection, using V4L2 format";
        width  = fmt.fmt.pix.width;
        height = fmt.fmt.pix.height;
    } else {
        /* V4L2 format is likely to be derived from media pad information, but, with
         * an event-based sensor, the packetization on streaming interfaces, such as
         * MIPI CSI-2, is independant from the sensor resolution. Since there should
         * be no event outside the crop bounds, processing should also happen within
         * those bounds
         */
        width  = crop_bound.r.width;
        height = crop_bound.r.height;
    }

    switch (fmt.fmt.pix.pixelformat) {
    case v4l2_fourcc('P', 'S', 'E', 'E'): {
        StreamFormat format("EVT2");
        format["width"]  = std::to_string(width);
        format["height"] = std::to_string(height);
        return format.to_string();
    }
    case v4l2_fourcc('P', 'S', 'E', '1'): {
        StreamFormat format("EVT21");
        format["endianness"] = "legacy";
        format["width"]      = std::to_string(width);
        format["height"]     = std::to_string(height);
        return format.to_string();
    }
    case v4l2_fourcc('P', 'S', 'E', '2'): {
        StreamFormat format("EVT21");
        format["width"]  = std::to_string(width);
        format["height"] = std::to_string(height);
        return format;
    }
    case v4l2_fourcc('P', 'S', 'E', '3'): {
        StreamFormat format("EVT3");
        format["width"]  = std::to_string(width);
        format["height"] = std::to_string(height);
        return format;
    }
    case v4l2_fourcc('G', 'R', 'E', 'Y'): {
        // evt format is supplied as user ctrl
        if(controls_->has("evt_format"))
        {
            auto& ctrl = controls_->get("evt_format");
            auto evtf = *ctrl.get_str();
            bool is_legacy = evtf == "EVT21ME";

            if(is_legacy)
                evtf = "EVT21";

            StreamFormat format(evtf);
            if(is_legacy)
                format["endianness"] = "legacy";

            format["width"]  = std::to_string(width);
            format["height"] = std::to_string(height);
            return format.to_string();
        }
        else
            throw std::runtime_error("Unsupported pixel format");
    }
    default:
        throw std::runtime_error("Unsupported pixel format");
    }
}

V4l2Capability V4L2DeviceControl::get_capability() const {
    return cap_;
}

const struct media_entity *V4L2DeviceControl::get_sensor_entity() const {
    auto sensor = std::find_if(entities_.begin(), entities_.end(),
                               [](const auto &entity) { return entity.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR; });

    if (sensor == entities_.end()) {
        throw HalConnectionException(ENODEV, std::generic_category(), "Could not find a v4l2 sensor subdevice");
    }

    return &(*sensor);
}

const struct media_entity *V4L2DeviceControl::get_video_entity() const {
    auto video = std::find_if(entities_.begin(), entities_.end(),
                              [](const auto &entity) { return entity.type == MEDIA_ENT_T_DEVNODE_V4L; });

    if (video == entities_.end()) {
        throw HalConnectionException(ENODEV, std::generic_category(), "Could not find a v4l2 video device");
    }

    return &(*video);
}

bool V4L2DeviceControl::can_crop(int fd) {
    struct v4l2_subdev_selection sel = {0};

    sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
    sel.pad    = 0;
    sel.target = V4L2_SEL_TGT_CROP_ACTIVE;
    if (ioctl(fd, VIDIOC_SUBDEV_G_CROP, &sel) == -EINVAL) {
        MV_HAL_LOG_TRACE() << "device can't crop";
        return false;
    }
    return true;
}

void V4L2DeviceControl::set_crop(int fd, const struct v4l2_rect &rect) {
    struct v4l2_subdev_selection sel = {0};

    sel.pad    = 0;
    sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
    sel.target = V4L2_SEL_TGT_CROP;
    sel.r      = rect;
    if (ioctl(fd, VIDIOC_SUBDEV_S_SELECTION, &sel) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_SUBDEV_S_SELECTION failed");
    }
}

void V4L2DeviceControl::get_native_size(int fd, struct v4l2_rect &rect) {
    struct v4l2_subdev_selection sel = {0};

    sel.pad    = 0;
    sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
    sel.target = V4L2_SEL_TGT_NATIVE_SIZE;
    if (ioctl(fd, VIDIOC_SUBDEV_G_SELECTION, &sel) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_SUBDEV_G_SELECTION failed");
    }
    rect = sel.r;
}

void V4L2DeviceControl::get_crop(int fd, struct v4l2_rect &rect) {
    struct v4l2_subdev_selection sel = {0};

    std::memset(&sel, 0, sizeof(sel));
    sel.pad    = 0;
    sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
    sel.target = V4L2_SEL_TGT_CROP;
    if (ioctl(fd, VIDIOC_SUBDEV_G_SELECTION, &sel) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_SUBDEV_G_SELECTION failed");
    }
    rect = sel.r;
}

void V4L2DeviceControl::start() {
    enum v4l2_buf_type type = buf_type_;
    if (ioctl(get_video_entity()->fd, VIDIOC_STREAMON, &type) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_STREAMON failed");
    }
}
void V4L2DeviceControl::stop() {
    enum v4l2_buf_type type = buf_type_;
    if (ioctl(get_video_entity()->fd, VIDIOC_STREAMOFF, &type) < 0) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_STREAMOFF failed");
    }
}
void V4L2DeviceControl::reset() {}

std::shared_ptr<V4L2Controls> V4L2DeviceControl::get_controls() {
    return controls_;
}

int V4L2DeviceControl::enumerate_entities() {
    struct media_entity entity;
    int ret = 0;
    int id = 0;

    for (id = 0; ; id = entity.desc.id) {

        char target[1024];
        const std::filesystem::path sys_base = "/sys/dev/char/";

        memset(&entity.desc, 0, sizeof(entity.desc));
        entity.desc.id = id | MEDIA_ENT_ID_FLAG_NEXT;
        ret = ioctl(media_fd_, MEDIA_IOC_ENUM_ENTITIES, &entity.desc);
        if (ret < 0) {
            if (errno == EINVAL) {
                break;
            }
            MV_HAL_LOG_TRACE() << "MEDIA_IOC_ENUM_ENTITIES ioctl failed:" << strerror(errno);
            return -1;
        }

        MV_HAL_LOG_TRACE() << "Found entity: " << entity.desc.name;
        std::filesystem::path sys_path = sys_base / (std::to_string(entity.desc.v4l.major) + ":" + std::to_string(entity.desc.v4l.minor));

        ret = readlink(sys_path.c_str(), target, sizeof(target));
        if (ret < 0) {
            MV_HAL_LOG_TRACE() << "Could not readlink" << sys_path << strerror(errno);
            return -1;
        }
        target[ret] = '\0';

        std::filesystem::path dev_path(target);
        std::filesystem::path devpath = std::filesystem::path("/dev/") / dev_path.filename();

        entity.path = devpath;
        entity.type = entity.desc.type;
        entity.fd = open(devpath.c_str(), O_RDWR);

        entities_.push_back(entity);
    }
    return 0;
}

