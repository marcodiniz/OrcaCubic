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

private:
    std::string m_host;
    std::string m_port;
    std::string m_token;
    std::string m_device_id;
    std::string m_mode_id;
    std::string m_device_name;

    std::string make_url(const std::string& path) const;
    bool query_info(wxString& error_msg) const;
};

} // namespace Slic3r

#endif // slic3r_AnycubicLink_hpp_
