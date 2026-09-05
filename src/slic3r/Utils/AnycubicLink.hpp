#ifndef slic3r_AnycubicLink_hpp_
#define slic3r_AnycubicLink_hpp_

#include <string>
#include <vector>
#include <map>
#include <wx/string.h>
#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class DynamicPrintConfig;

struct AnycubicMaterialSlot {
    int slot_id{-1};
    int box_id{-1};
    std::string type;
    std::string color;
    bool loaded{false};
    float weight{0.f};
};

struct AnycubicToolFilament {
    int tool_id{-1};
    std::string type;
    std::string color;
};

struct AnycubicAmsMappingEntry {
    int ams_index{-1};
    int paint_index{-1};
    std::string material_type;
    std::string ams_color;
    std::string paint_color;
};

struct AnycubicPrintSettings {
    bool auto_leveling{true};
    bool vibration_compensation{false};
    bool flow_calibration{false};
    bool timelapse{false};
};

struct AnycubicTaskSettings {
    int auto_leveling{1};
    int vibration_compensation{0};
    int flow_calibration{0};
    int timelapse_status{0};
};

std::vector<AnycubicAmsMappingEntry> build_anycubic_ams_mapping(
    const std::vector<AnycubicToolFilament>& tools,
    const std::vector<AnycubicMaterialSlot>& slots,
    const std::vector<int>& selected_slot_ids);
AnycubicTaskSettings build_anycubic_task_settings(const AnycubicPrintSettings& settings);
void shutdown_anycubic_lan_bridge();

class AnycubicLink : public PrintHost
{
public:
    explicit AnycubicLink(DynamicPrintConfig *config);
    ~AnycubicLink() override = default;

    const char* get_name() const override { return "AnycubicLink"; }
    static std::string get_print_host_webui(DynamicPrintConfig *config);

    bool                       test(wxString &curl_msg) const override;
    wxString                   get_test_ok_msg() const override;
    wxString                   get_test_failed_msg(wxString &msg) const override;
    bool                       upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool                       has_auto_discovery() const override { return false; }
    bool                       can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string                get_host() const override { return m_host; }

    bool fetch_material_slots(std::vector<AnycubicMaterialSlot>& slots, wxString& msg) const;

    const std::string& get_device_id() const { return m_device_id; }
    const std::string& get_mode_id() const { return m_mode_id; }
    const std::string& get_token() const { return m_token; }
    const std::string& get_broker() const { return m_broker; }
    const std::string& get_username() const { return m_username; }

    bool fetch_credentials(wxString& error_msg) const;
    bool fetch_upload_url_via_mqtt(std::string& upload_token, wxString& error_msg) const;
    bool start_print(wxString& error_msg, const std::string& filename, const PrintHostUpload& upload_data) const;

private:
    std::string m_host;
    std::string m_port;
    std::string m_token;
    std::string m_device_id;
    std::string m_mode_id;
    std::string m_device_name;
    std::string m_cn;
    std::string m_ctrl_url;
    std::string m_broker;
    std::string m_username;
    std::string m_password;
    std::string m_device_crt;
    std::string m_device_pk;
    std::string m_upload_token;

    std::string make_url(const std::string& path) const;
    bool query_info(wxString& error_msg) const;
};

} // namespace Slic3r

#endif // slic3r_AnycubicLink_hpp_
