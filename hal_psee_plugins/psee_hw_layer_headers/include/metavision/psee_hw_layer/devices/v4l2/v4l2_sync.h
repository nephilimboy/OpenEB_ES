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

#ifndef METAVISION_HAL_V4L2_SYNC_H
#define METAVISION_HAL_V4L2_SYNC_H

#include <string>
#include <map>

#include "metavision/hal/facilities/i_camera_synchronization.h"
#include "metavision/psee_hw_layer/boards/v4l2/v4l2_controls.h"

namespace Metavision {

class V4l2Synchronization : public I_CameraSynchronization {
public:
    V4l2Synchronization(std::shared_ptr<V4L2Controls> controls);
    virtual bool set_mode_standalone() override;
    virtual bool set_mode_master() override;
    virtual bool set_mode_slave() override;
    virtual SyncMode get_mode() const override;
private:
    std::shared_ptr<V4L2Controls> controls_;
};

} // namespace Metavision

#endif // METAVISION_HAL_V4L2_SYNC_H
