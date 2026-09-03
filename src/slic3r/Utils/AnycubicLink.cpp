#include "AnycubicLink.hpp"

#include <algorithm>
#include <sstream>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "Http.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace Slic3r {

namespace {

std::string safe_config_str(DynamicPrintConfig* config, const char* key)
{
    if (config == nullptr)
        return {};
    if (const auto* opt = config->option<ConfigOptionString>(key); opt != nullptr)
        return opt->value;
    return {};
}

std::string sanitize_filename(const std::string& filename)
{
    std::string base = fs::path(filename).filename().string();
    if (base.empty())
        base = "print.gcode";
    return base;
}

} // namespace

AnycubicLink::AnycubicLink(DynamicPrintConfig *config)
    : m_port("18910")
{
    std::string raw_host = safe_config_str(config, "print_host");
    boost::trim(raw_host);

    // Strip http:// or https://
    if (boost::starts_with(raw_host, "http://"))
        raw_host = raw_host.substr(7);
    else if (boost::starts_with(raw_host, "https://"))
        raw_host = raw_host.substr(8);

    // Split host:port if user entered custom port
    auto colon_pos = raw_host.find(':');
    if (colon_pos != std::string::npos) {
        m_host = raw_host.substr(0, colon_pos);
        m_port = raw_host.substr(colon_pos + 1);
    } else {
        m_host = raw_host;
    }
}

std::string AnycubicLink::make_url(const std::string& path) const
{
    std::string clean_path = path;
    if (!clean_path.empty() && clean_path[0] == '/')
        clean_path = clean_path.substr(1);
    return (boost::format("http://%1%:%2%/%3%") % m_host % m_port % clean_path).str();
}

std::string AnycubicLink::get_print_host_webui(DynamicPrintConfig *config)
{
    if (config == nullptr)
        return {};

    std::string webui = safe_config_str(config, "print_host_webui");
    if (!webui.empty())
        return webui;

    // Default to official Anycubic Workbench web UI
    return "https://cloud-universe.anycubic.com/w/p/AcOrcaWeb/workbench/";
}

bool AnycubicLink::query_info(wxString& error_msg) const
{
    if (m_host.empty()) {
        error_msg = _(L("Printer host IP address is empty."));
        return false;
    }

    auto url = make_url("info");
    bool success = false;
    std::string response_body;

    auto http = Http::get(url);
    http.header("User-Agent", "AnycubicSlicerNext/2.0.0.2")
        .timeout(5)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                success = true;
            } else {
                error_msg = GUI::from_u8((boost::format("HTTP status %1%") % status).str());
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            error_msg = GUI::from_u8((boost::format("%1% (HTTP %2%)") % error % status).str());
        })
        .perform_sync();

    if (!success)
        return false;

    auto parsed = json::parse(response_body, nullptr, false, true);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error_msg = _(L("Invalid JSON response from printer info endpoint."));
        return false;
    }

    const_cast<AnycubicLink*>(this)->m_token       = parsed.value("token", "");
    const_cast<AnycubicLink*>(this)->m_mode_id     = parsed.value("modelId", "");
    const_cast<AnycubicLink*>(this)->m_device_name = parsed.value("deviceName", parsed.value("modelName", "Anycubic Printer"));
    const_cast<AnycubicLink*>(this)->m_cn          = parsed.value("cn", "");

    return true;
}

bool AnycubicLink::test(wxString &curl_msg) const
{
    wxString error;
    if (!query_info(error)) {
        curl_msg = error;
        return false;
    }

    curl_msg = GUI::from_u8((boost::format("%1% (Model ID: %2%, CN: %3%)") % m_device_name % m_mode_id % m_cn).str());
    return true;
}

wxString AnycubicLink::get_test_ok_msg() const
{
    return GUI::from_u8((boost::format(_utf8(L("Connected to %1% successfully over LAN."))) % m_device_name).str());
}

wxString AnycubicLink::get_test_failed_msg(wxString &msg) const
{
    return GUI::from_u8((boost::format(_utf8(L("Could not connect to Anycubic printer at %1%:%2%: %3%"))) % m_host % m_port % msg.ToUTF8().data()).str());
}

bool AnycubicLink::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    wxString query_err;
    if (!query_info(query_err)) {
        error_fn(GUI::from_u8((boost::format("Failed to reach printer before upload: %1%") % query_err.ToUTF8().data()).str()));
        return false;
    }

    std::string upload_filename = sanitize_filename(upload_data.upload_path.string());
    std::string file_size_str;
    try {
        file_size_str = std::to_string(fs::file_size(upload_data.source_path));
    } catch (...) {
        file_size_str = "0";
    }

    std::string upload_endpoint = "gcode_upload";
    if (!m_token.empty())
        upload_endpoint += "?s=" + m_token;

    auto url = make_url(upload_endpoint);
    bool upload_ok = false;
    std::string response_payload;

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Uploading file " << upload_filename << " (" << file_size_str << " bytes) to " << url;

    auto http = Http::post(url);
    http.header("User-Agent", "AnycubicSlicerNext/2.0.0.2")
        .header("X-BBL-Client-Name", "AnycubicSlicerNext")
        .header("X-File-Length", file_size_str)
        .form_add("filename", upload_filename)
        .form_add_file("gcode", upload_data.source_path.string(), upload_filename)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Upload complete with status " << status << ", body: " << body;
            if (status == 200) {
                upload_ok = true;
                response_payload = std::move(body);
            } else {
                error_fn(GUI::from_u8((boost::format("Printer upload returned HTTP %1%: %2%") % status % body).str()));
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Upload error: " << error << " HTTP " << status;
            error_fn(GUI::from_u8((boost::format("Upload failed: %1% (HTTP %2%)") % error % status).str()));
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel)
                upload_ok = false;
        })
        .perform_sync();

    return upload_ok;
}

bool AnycubicLink::fetch_material_slots(std::vector<AnycubicMaterialSlot>& slots, wxString& msg) const
{
    slots.clear();
    return true;
}

} // namespace Slic3r
