#include "boards/v4l2/v4l2_hardware_identification.h"
#include "metavision/hal/facilities/i_plugin_software_info.h"
#include "boards/v4l2/v4l2_device.h"
#include "metavision/psee_hw_layer/utils/psee_format.h"
#include "metavision/psee_hw_layer/boards/rawfile/psee_raw_file_header.h"
#include <fcntl.h>

namespace Metavision {

V4l2HwIdentification::V4l2HwIdentification(std::shared_ptr<V4L2DeviceControl> ctrl,
                                           const std::shared_ptr<I_PluginSoftwareInfo> &plugin_sw_info) :
    I_HW_Identification(plugin_sw_info), ctrl_(ctrl) {}

I_HW_Identification::SensorInfo V4l2HwIdentification::get_sensor_info() const {
    auto controls = ctrl_->get_controls();
    auto sensor_ent = ctrl_->get_sensor_entity();
    std::string ent_name = std::string(sensor_ent->desc.name);

    if (ent_name.find("imx636") == 0) {
        return {4, 2, "IMX636"};
    } else if (ent_name.find("genx320") == 0) {
        return {320, 0, "GenX320"};
    } else {
        return {0, 0, "Unknown sensor"};
    }
}

std::vector<std::string> V4l2HwIdentification::get_available_data_encoding_formats() const {
    StreamFormat format(get_current_data_encoding_format());
    return {format.name()};
}

std::string V4l2HwIdentification::get_current_data_encoding_format() const {
    return ctrl_->get_format().to_string();
}

std::string V4l2HwIdentification::get_serial() const {
    std::stringstream ss;
    ss << ctrl_->get_sensor_entity()->desc.name;
    return ss.str();
}
std::string V4l2HwIdentification::get_integrator() const {
    std::stringstream ss;
    ss << ctrl_->get_capability().driver;
    return ss.str();
}
std::string V4l2HwIdentification::get_connection_type() const {
    std::stringstream ss;
    ss << ctrl_->get_capability().bus_info;
    return ss.str();
}

DeviceConfigOptionMap V4l2HwIdentification::get_device_config_options_impl() const {
    return {{"ll_biases_range_check_bypass", DeviceConfigOption(true)}};
}

RawFileHeader V4l2HwIdentification::get_header_impl() const {
    return PseeRawFileHeader(*this);
}
} // namespace Metavision
