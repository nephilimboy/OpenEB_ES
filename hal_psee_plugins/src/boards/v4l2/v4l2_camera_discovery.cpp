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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <sys/ioctl.h>

#include "boards/v4l2/v4l2_device.h"
#include "boards/v4l2/v4l2_camera_discovery.h"
#include "boards/v4l2/v4l2_data_transfer.h"

#include "metavision/hal/device/device_discovery.h"
#include "metavision/hal/plugin/plugin_entrypoint.h"
#include "metavision/hal/utils/device_builder.h"
#include "metavision/hal/utils/hal_log.h"
#include "metavision/psee_hw_layer/utils/psee_format.h"
#include "metavision/psee_hw_layer/boards/v4l2/v4l2_board_command.h"
#include <filesystem>


namespace Metavision {

V4l2CameraDiscovery::V4l2CameraDiscovery() {
    for (const auto &entry : std::filesystem::directory_iterator("/dev/")) {
        if (entry.path().string().find("/dev/media") == 0) {
            try {
                devices_.emplace_back(std::make_shared<V4L2BoardCommand>(entry.path()));
            } catch (const std::exception &e) {
                // This does not have to crash because only valid devices are added.
                MV_HAL_LOG_TRACE() << "Discarding " << entry.path().string();
                MV_HAL_LOG_TRACE() << e.what();
            }
        }
    }
    MV_HAL_LOG_TRACE() << "V4l2Discovery: found " << devices_.size() << " devices.";
}

bool V4l2CameraDiscovery::is_for_local_camera() const {
    return true;
}

CameraDiscovery::SerialList V4l2CameraDiscovery::list() {
    SerialList serial_list;
    for (const auto &device : devices_) {
        serial_list.emplace_back(device->get_serial());
    }
    return serial_list;
}

CameraDiscovery::SystemList V4l2CameraDiscovery::list_available_sources() {
    SystemList system_list;
    for (const auto &device : devices_) {
        system_list.push_back({device->get_serial(), ConnectionType::MIPI_LINK});
    }
    return system_list;
}

bool V4l2CameraDiscovery::discover(DeviceBuilder &device_builder, const std::string &serial,
                                   const DeviceConfig &config) {
    if (devices_.empty()) {
        return false;
    }

    size_t n_devices  = devices_.size();
    size_t dev_select = 0;

    for (size_t i = 0; i < n_devices; ++i) {
        if (devices_[i]->get_serial() == serial) {
            dev_select = i;
        }
    }

    auto res = false;
    try {
        res = builder->build_device(devices_[dev_select], device_builder, config);
    } catch (std::exception &e) { MV_HAL_LOG_ERROR() << "Failed to build streaming facilities :" << e.what(); }

    MV_HAL_LOG_TRACE() << "V4l2Discovery " << (res ? "success with serial: " : "failure for serial: ") << serial;
    return res;
}

} // namespace Metavision
