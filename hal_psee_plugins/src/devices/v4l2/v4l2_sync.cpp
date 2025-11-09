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

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>

#include "metavision/psee_hw_layer/devices/v4l2/v4l2_sync.h"
#include "metavision/psee_hw_layer/boards/v4l2/v4l2_controls.h"
#include "metavision/hal/utils/hal_log.h"
#include "metavision/hal/utils/hal_exception.h"

namespace Metavision {

V4l2Synchronization::V4l2Synchronization(std::shared_ptr<V4L2Controls> controls) : controls_(controls) {
    // reset all erc controls to default values.
    controls_->foreach ([&](V4L2Controls::V4L2Control &ctrl) {
        auto name = std::string(ctrl.query_.name);
        // skip non erc controls
        if (name.find("sync_mode") != 0) {
            return 0;
        }
        ctrl.reset();
        return 0;
    });
}

bool V4l2Synchronization::set_mode_standalone() {
    auto ctrl = controls_->get("sync_mode");
    auto smode = I_CameraSynchronization::SyncMode::STANDALONE;
    int ret = ctrl.set_menu(static_cast<int>(smode));
    if (ret != 0) {
        MV_HAL_LOG_ERROR() << "Failed to set sync_mode Control value to STANDALONE";
        return false;
    }
    MV_HAL_LOG_INFO() << "Set sync_mode Control value to STANDALONE";
    return true;
}

bool V4l2Synchronization::set_mode_master() {
    auto ctrl = controls_->get("sync_mode");
    auto smode = I_CameraSynchronization::SyncMode::MASTER;
    int ret = ctrl.set_menu(static_cast<int>(smode));
    if (ret != 0) {
        MV_HAL_LOG_ERROR() << "Failed to set sync_mode Control value to MASTER";
        return false;
    }
    MV_HAL_LOG_INFO() << "Set sync_mode Control value to MASTER";
    return true;
}

bool V4l2Synchronization::set_mode_slave() {
    auto ctrl = controls_->get("sync_mode");
    auto smode = I_CameraSynchronization::SyncMode::SLAVE;
    int ret = ctrl.set_menu(static_cast<int>(smode));
    if (ret != 0) {
        MV_HAL_LOG_ERROR() << "Failed to set sync_mode Control value to SLAVE";
        return false;
    }
    MV_HAL_LOG_INFO() << "Set sync_mode Control value to SLAVE";
    return true;
}

I_CameraSynchronization::SyncMode V4l2Synchronization::get_mode() const {
    auto ctrl = controls_->get("sync_mode");
    int ret = *ctrl.get_int();
    return I_CameraSynchronization::SyncMode(ret);
}

} // namespace Metavision
