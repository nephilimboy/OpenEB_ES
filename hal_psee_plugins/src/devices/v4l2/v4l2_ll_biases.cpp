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

#include <cassert>
#include <cmath>
#include "metavision/hal/utils/detail/hal_log_impl.h"
#include "metavision/psee_hw_layer/devices/v4l2/v4l2_ll_biases.h"
#include "metavision/psee_hw_layer/boards/v4l2/v4l2_controls.h"

namespace Metavision {

V4L2LLBiases::V4L2LLBiases(const DeviceConfig &device_config, const std::shared_ptr<I_HW_Identification> &hw_identification, std::shared_ptr<V4L2Controls> controls, bool relative) :
    I_LL_Biases(device_config), hw_identification_(hw_identification), controls_(controls) { 
    auto sensor_info = hw_identification->get_sensor_info();
    if (sensor_info.name_ == "IMX636") {
        relative_ = true;
	    bias_bitwidth_ = 8;
    } else {
        relative_ = false;
	    bias_bitwidth_ = 7;
    }

    MV_HAL_LOG_TRACE() << "V4L2 Biases - Sensor info: " << sensor_info.name_;
    MV_HAL_LOG_TRACE() << "V4L2 Biases - Relative   : " << relative_;
    MV_HAL_LOG_TRACE() << "V4L2 Biases - Bit Width  : " << pow(2, bias_bitwidth_) - 1;

    // reset all biases to default values
    controls_->foreach ([&](V4L2Controls::V4L2Control &ctrl) {
        auto name = std::string(ctrl.query_.name);
        // skip non bias controls
        if (name.find("bias_") != 0) {
            return 0;
        }

        ctrl.reset();
        return 0;
    });
}

bool V4L2LLBiases::set_impl(const std::string &bias_name, int bias_value) {
    auto ctrl = controls_->get(bias_name);
    int ret;

    if (relative_) {
        int current_val = get_impl(bias_name);
        bias_value += ctrl.query_.default_value;
    }

    ret = ctrl.set_int(bias_value);
    if (ret != 0) {
        MV_HAL_LOG_ERROR() << "Failed to set" << bias_name << "Control value to" << bias_value;
        return false;
    }

    MV_HAL_LOG_INFO() << "Success setting" << bias_name << "Control value to" << bias_value;
    return true;
}

int V4L2LLBiases::get_impl(const std::string &bias_name) const {
    auto ctrl      = controls_->get(bias_name);
    auto maybe_val = ctrl.get_int();
    if (!maybe_val.has_value())
        throw std::runtime_error("could not get control value");

    MV_HAL_LOG_INFO() << bias_name << "Control value:" << *maybe_val;
    return relative_ ? *maybe_val - ctrl.query_.default_value : *maybe_val;
}

bool V4L2LLBiases::get_bias_info_impl(const std::string &bias_name, LL_Bias_Info &bias_info) const {
    auto ctrl = controls_->get(bias_name);
    int offset = relative_ ? ctrl.query_.default_value : 0;
    bias_info = LL_Bias_Info(0 - offset, pow(2, bias_bitwidth_) - 1 - offset, 
                             ctrl.query_.minimum - offset, ctrl.query_.maximum - offset, 
                             std::string("todo::description"), true, std::string("todo::category"));

    return true;
}

std::map<std::string, int> V4L2LLBiases::get_all_biases() const {
    std::map<std::string, int> biases;

    controls_->foreach ([&biases, this](V4L2Controls::V4L2Control &ctrl) {
        auto name = std::string(ctrl.query_.name);
        // skip non bias controls
        if (name.find("bias_") != 0) {
            return 0;
        }

        auto maybe_val = ctrl.get_int();
        if (!maybe_val.has_value()) {
            return 0;
        }

        if (relative_) {
            biases[ctrl.query_.name] = *maybe_val - ctrl.query_.default_value;
        } else {
            biases[ctrl.query_.name] = *maybe_val;
        }

        return 0;
    });

    return biases;
}

} // namespace Metavision
