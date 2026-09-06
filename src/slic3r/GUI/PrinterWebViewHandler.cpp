#include "PrinterWebViewHandler.hpp"

#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "PrinterWebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/AnycubicLink.hpp"
#include "libslic3r/Preset.hpp"

#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <map>
#include <thread>
#include <wx/filedlg.h>
#include <wx/string.h>
#include <wx/webview.h>

using json = nlohmann::json;

namespace Slic3r {
namespace GUI {

PrinterWebViewHandler::PrinterWebViewHandler(PrinterWebView& owner)
    : m_owner(owner)
{
}

PrinterWebViewHandler::~PrinterWebViewHandler() = default;

void PrinterWebViewHandler::on_loaded(wxWebViewEvent &evt)
{
}

void PrinterWebViewHandler::on_script_message(wxWebViewEvent &evt)
{
}

PrinterWebView& PrinterWebViewHandler::owner() const
{
    return m_owner;
}

wxWebView* PrinterWebViewHandler::browser() const
{
    return m_owner.m_browser;
}

namespace {

DynamicPrintConfig* get_active_printer_config()
{
    if (wxGetApp().preset_bundle == nullptr)
        return nullptr;

    return &wxGetApp().preset_bundle->printers.get_edited_preset().config;
}

std::string json_string(const json& node, const char* key)
{
    auto it = node.find(key);
    return (it != node.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::string dump_json(const json& node)
{
    return node.dump(-1, ' ', false, json::error_handler_t::replace);
}

boost::filesystem::path path_from_utf8(const std::string& utf8_path)
{
#ifdef _WIN32
    const wxString wide_path = wxString::FromUTF8(utf8_path.c_str());
    return boost::filesystem::path(wide_path.ToStdWstring());
#else
    return boost::filesystem::path(utf8_path);
#endif
}

std::string filename_to_utf8(const boost::filesystem::path& path)
{
#ifdef _WIN32
    const wxString wx_filename(path.filename().c_str());
    const wxScopedCharBuffer utf8 = wx_filename.ToUTF8();
    return utf8.data() != nullptr ? std::string(utf8.data()) : std::string();
#else
    return path.filename().string();
#endif
}

bool parse_hex_color(const std::string& hex, int& r, int& g, int& b)
{
    std::string clean = hex;
    if (!clean.empty() && clean[0] == '#')
        clean = clean.substr(1);
    if (clean.length() == 3) {
        clean = { clean[0], clean[0], clean[1], clean[1], clean[2], clean[2] };
    }
    if (clean.length() != 6)
        return false;
    try {
        r = std::stoi(clean.substr(0, 2), nullptr, 16);
        g = std::stoi(clean.substr(2, 2), nullptr, 16);
        b = std::stoi(clean.substr(4, 2), nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

std::string format_hex_color(const std::string& hex)
{
    if (hex.empty())
        return "#FFFFFF";
    if (hex[0] == '#')
        return hex;
    return "#" + hex;
}

class ElegooPrinterWebViewHandler final : public PrinterWebViewHandler {
public:
    explicit ElegooPrinterWebViewHandler(PrinterWebView& owner)
        : PrinterWebViewHandler(owner)
    {
    }

    ~ElegooPrinterWebViewHandler() override
    {
        stop_upload = true;
        if (upload_thread.joinable())
            upload_thread.join();
    }

    void on_script_message(wxWebViewEvent &evt) override
    {
        const wxString message = evt.GetString();
        if (message.empty())
            return;

        json root = json::parse(message.ToUTF8().data(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return;

        std::string request_id = json_string(root, "id");
        std::string method     = json_string(root, "method");
        json        params     = root.contains("params") && root["params"].is_object() ? root["params"] : json::object();

        if (method.empty()) {
            method = json_string(root, "command");
            if (params.empty() && root.contains("data") && root["data"].is_object())
                params = root["data"];
        }

        if (method == "open" || method == "common_openurl") {
            const std::string url = json_string(params, "url").empty() ? json_string(root, "url") : json_string(params, "url");
            if (!url.empty())
                wxLaunchDefaultBrowser(url);
            if (!request_id.empty())
                send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        if (method == "upload_file") {
            handle_upload_request(request_id, method, dump_json(params));
            return;
        }

        if (method == "open_file_dialog") {
            handle_open_file_dialog_request(request_id, method, dump_json(params));
            return;
        }

        if (method == "get_sn") {
            handle_get_sn_request(request_id, method);
            return;
        }
    }

private:
    void send_ipc_message(const char* type, const std::string& request_id, const std::string& method, int code,
                          const std::string& message, const std::string& data_json = "{}")
    {
        if (browser() == nullptr)
            return;

        json body = json::object();
        body["type"] = type;
        if (!request_id.empty())
            body["id"] = request_id;
        if (!method.empty())
            body["method"] = method;

        json data = json::parse(data_json, nullptr, false);
        if (data.is_discarded())
            data = json::object();
        body["data"] = std::move(data);

        if (std::string(type) == "response") {
            body["code"] = code;
            body["message"] = message;
        }

        const wxString payload = wxString::FromUTF8(dump_json(body));
        const wxString script = "if (typeof HandleStudio === 'function') { HandleStudio(" + payload + "); } else { window.postMessage(" + payload + ", '*'); }";
        wxGetApp().CallAfter([this, script]() {
            if (browser() != nullptr)
                WebView::RunScript(browser(), script);
        });
    }

    void handle_upload_request(const std::string& request_id, const std::string& method, const std::string& params_json)
    {
        if (upload_in_progress.exchange(true)) {
            send_ipc_message("response", request_id, method, 1, "Upload already in progress");
            return;
        }

        if (upload_thread.joinable())
            upload_thread.join();

        json params = json::parse(params_json, nullptr, false);
        if (params.is_discarded())
            params = json::object();

        std::string file_path = json_string(params, "filePath");
        std::string file_name = json_string(params, "fileName");

        if (file_path.empty()) {
            upload_in_progress = false;
            send_ipc_message("response", request_id, method, 1, "Missing filePath");
            return;
        }

        // HTML IPC passes UTF-8 strings; decode explicitly to avoid Windows codepage issues.
        boost::filesystem::path source_path = path_from_utf8(file_path);
        if (file_name.empty())
            file_name = filename_to_utf8(source_path);

        DynamicPrintConfig* config = get_active_printer_config();
        std::unique_ptr<PrintHost> print_host(config == nullptr ? nullptr : PrintHost::get_print_host(config));
        if (print_host == nullptr) {
            upload_in_progress = false;
            send_ipc_message("response", request_id, method, 1, "Could not get a valid Printer Host reference");
            return;
        }

        stop_upload = false;
        upload_thread = std::thread([this, request_id, method, file_path, file_name, source_path, print_host = std::move(print_host)]() mutable {
            std::string error_message;

            PrintHostUpload upload_data;
            upload_data.use_3mf      = false;
            upload_data.post_action  = PrintHostPostUploadAction::None;
            upload_data.source_path  = source_path;
            upload_data.upload_path  = path_from_utf8(file_name);

            const bool success = print_host->upload(
                std::move(upload_data),
                [this, request_id](Http::Progress progress, bool& cancel) {
                    cancel = stop_upload.load();
                    json data = {
                        {"uploadedBytes", static_cast<uint64_t>(progress.ulnow)},
                        {"totalBytes", static_cast<uint64_t>(progress.ultotal)}
                    };
                    send_ipc_message("event", request_id, "upload_progress", 0, "", dump_json(data));
                },
                [&error_message](wxString error) {
                    error_message = error.ToUTF8().data();
                },
                [this, request_id](wxString tag, wxString status) {
                    json data = {
                        {"tag", tag.ToUTF8().data()},
                        {"status", status.ToUTF8().data()}
                    };
                    send_ipc_message("event", request_id, "upload_info", 0, "", dump_json(data));
                });

            upload_in_progress = false;

            if (success) {
                json data = {
                    {"success", true},
                    {"filePath", file_path},
                    {"fileName", file_name}
                };
                send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
            } else {
                if (error_message.empty())
                    error_message = "Upload failed";
                send_ipc_message("response", request_id, method, 1, error_message);
            }
        });
    }

    void handle_open_file_dialog_request(const std::string& request_id, const std::string& method, const std::string& params_json)
    {
        json params = json::parse(params_json, nullptr, false);
        if (params.is_discarded())
            params = json::object();

        const std::string filter = json_string(params, "filter").empty() ? "All files (*.*)|*.*" : json_string(params, "filter");

        wxWindow* parent = owner().GetParent();
        if (parent == nullptr)
            parent = wxGetApp().GetTopWindow();

        wxFileDialog open_file_dialog(parent, _L("Open File"), "", "", wxString::FromUTF8(filter), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        json data = json::object();
        data["files"] = json::array();
        if (open_file_dialog.ShowModal() != wxID_CANCEL)
            data["files"].push_back(open_file_dialog.GetPath().ToUTF8().data());

        send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
    }

    void handle_get_sn_request(const std::string& request_id, const std::string& method)
    {
        // Panel always calls get_sn with a 10s IPC timeout. Answer immediately from
        // dev_sn / cache — do not spawn a thread or perform HTTP (panel uses URL sn on miss).
        std::string sn;
        if (DynamicPrintConfig* config = get_active_printer_config()) {
            const std::unique_ptr<PrintHost> host(PrintHost::get_print_host(config));
            if (host)
                sn = host->get_sn();
        }
        json data = { { "sn", sn } };
        send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
    }

    std::atomic<bool> upload_in_progress { false };
    std::atomic<bool> stop_upload { false };
    std::thread       upload_thread;
};

class AnycubicPrinterWebViewHandler final : public PrinterWebViewHandler {
public:
    explicit AnycubicPrinterWebViewHandler(PrinterWebView& owner)
        : PrinterWebViewHandler(owner)
    {
        owner.set_bridge_token(wxString::FromUTF8(anycubic_lan_bridge_token()));
    }

    ~AnycubicPrinterWebViewHandler() override
    {
        stop_upload = true;
        if (upload_thread.joinable())
            upload_thread.join();
    }

    void on_loaded(wxWebViewEvent &evt) override
    {
        BOOST_LOG_TRIVIAL(info) << "[AnycubicPrinterWebViewHandler] on_loaded: " << evt.GetURL().ToUTF8().data();
        inject_bridge_token();
        inject_ac_localhost_interceptor();
        sync_printers_to_webview();
        sync_filaments_to_webview();
        sync_login_info_to_webview();
    }

    void on_script_message(wxWebViewEvent &evt) override
    {
        const wxString message = evt.GetString();
        if (message.empty())
            return;

        BOOST_LOG_TRIVIAL(info) << "[AnycubicPrinterWebViewHandler] on_script_message: " << message.ToUTF8().data();

        json root = json::parse(message.ToUTF8().data(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return;

        std::string request_id = json_string(root, "sequence_id");
        if (request_id.empty())
            request_id = json_string(root, "id");

        std::string method = json_string(root, "command");
        if (method.empty())
            method = json_string(root, "method");

        json params = json::object();
        if (root.contains("data") && root["data"].is_object())
            params = root["data"];
        else if (root.contains("params") && root["params"].is_object())
            params = root["params"];

        // 1. Filament & spool synchronization
        if (method == "sync_filaments_to_slicer") {
            wxGetApp().CallAfter([]() {
                wxGetApp().sidebar().sync_ams_list(true);
            });
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }
        if (method == "get_anycubic_printers") {
            sync_printers_to_webview(request_id);
            return;
        }
        if (method == "monitor_anycubic_printer") {
            select_printer(request_id, json_string(params, "preset_name"), false);
            return;
        }
        if (method == "activate_anycubic_printer") {
            select_printer(request_id, json_string(params, "preset_name"), true);
            return;
        }
        if (method == "open_anycubic_printer_settings") {
            wxGetApp().CallAfter([]() {
                PhysicalPrinterDialog dialog(wxGetApp().mainframe);
                dialog.ShowModal();
            });
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }
        if (method == "request_plate_filaments_to_printer" || method == "get_active_filaments" || method == "request_filament_list" ||
            method == "sync_filaments" || method == "get_filaments" ||
            method == "get_filament_info" || method == "request_3mf_info") {
            sync_filaments_to_webview(request_id);
            return;
        }

        // 2. User login & session status
        if (method == "get_login_info") {
            sync_login_info_to_webview(request_id);
            wxGetApp().CallAfter([]() {
                wxGetApp().get_login_info();
            });
            return;
        }
        if (method == "homepage_login_or_register" || method == "login") {
            wxGetApp().CallAfter([]() {
                wxGetApp().request_login(true);
            });
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }
        if (method == "homepage_logout" || method == "logout") {
            wxGetApp().CallAfter([]() {
                wxGetApp().request_user_logout();
            });
            sync_login_info_to_webview(request_id);
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        // 3. Print & Upload requests
        if (method == "start_print" || method == "print_gcode" || method == "print_plate" ||
            method == "print" || method == "upload_file") {
            handle_print_request(request_id, method, params);
            return;
        }

        // 4. Open External URLs
        if (method == "open" || method == "common_openurl" || method == "userguide_wiki_open") {
            std::string url = json_string(params, "url");
            if (url.empty())
                url = json_string(root, "url");
            if (!url.empty())
                wxLaunchDefaultBrowser(url);
            if (!request_id.empty())
                send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        // 5. Open Project / Import model
        if (method == "open_project" || method == "request_project_download" ||
            method == "import_model" || method == "homepage_open_recentfile" ||
            method == "modelmall_model_open" || method == "makerworld_model_open") {
            handle_open_project_request(request_id, method, params);
            return;
        }

        // 6. Open file dialog
        if (method == "open_file_dialog") {
            handle_open_file_dialog_request(request_id, method, dump_json(params));
            return;
        }

        // 7. Get Serial Number / Device ID
        if (method == "get_sn") {
            handle_get_sn_request(request_id, method);
            return;
        }

        // 8. Multi-color box configuration acknowledgment (setInfo / setExtfilbox)
        if (method == "setInfo" || method == "setExtfilbox") {
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        // 9. Fallback to GUI_App::handle_web_request for Anycubic SlicerNext built-ins
        std::string raw_msg = message.ToUTF8().data();
        std::string response = wxGetApp().handle_web_request(raw_msg);
        if (!response.empty()) {
            response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
            const wxString script = wxString::Format(
                "if (typeof HandleStudio === 'function') { HandleStudio(%s); } else { window.postMessage(%s, '*'); }",
                wxString::FromUTF8(response), wxString::FromUTF8(response));
            wxGetApp().CallAfter([this, script]() {
                if (browser() != nullptr)
                    WebView::RunScript(browser(), script);
            });
        }
    }

private:
    std::vector<AnycubicPrinterCandidate> printer_candidates() const
    {
        std::vector<AnycubicPrinterCandidate> candidates;
        if (wxGetApp().preset_bundle == nullptr)
            return candidates;

        const std::string selected_name = wxGetApp().preset_bundle->printers.get_edited_preset().name;
        for (const Preset& preset : wxGetApp().preset_bundle->printers.get_presets()) {
            const auto* host_type = preset.config.option<ConfigOptionEnum<PrintHostType>>("host_type");
            const auto* host = preset.config.option<ConfigOptionString>("print_host");
            if (host_type == nullptr || host == nullptr)
                continue;

            const auto* model = preset.config.option<ConfigOptionString>("printer_model");
            candidates.push_back({preset.name,
                                  model == nullptr ? std::string{} : model->value,
                                  host_type->value == htAnycubic ? "anycubic" : "other",
                                  host->value,
                                  preset.name == selected_name});
        }
        return candidates;
    }

    void sync_printers_to_webview(const std::string& request_id = "")
    {
        std::string active_host;
        if (DynamicPrintConfig* config = get_active_printer_config())
            if (const auto* host = config->option<ConfigOptionString>("print_host"))
                active_host = host->value;

        json items = json::array();
        for (const AnycubicPrinterListEntry& printer : build_anycubic_printer_list(
                 printer_candidates(), active_host, anycubic_lan_bridge_host())) {
            items.push_back({{"preset_name", printer.preset_name},
                             {"name", printer.preset_name},
                             {"model", printer.model_name.empty() ? "Anycubic Printer" : printer.model_name},
                             {"host", printer.host},
                             {"active", printer.active},
                             {"monitored", printer.monitored},
                             {"selected", printer.selected}});
        }
        send_ipc_message("response", request_id, "get_anycubic_printers", 0, "success",
                         json({{"printers", std::move(items)}}).dump());
    }

    void select_printer(const std::string& request_id, const std::string& preset_name, bool make_active)
    {
        const std::vector<AnycubicPrinterCandidate> candidates = printer_candidates();
        const auto selection = choose_anycubic_printer(candidates, preset_name, make_active);
        const char* method = make_active ? "activate_anycubic_printer" : "monitor_anycubic_printer";
        if (!selection) {
            send_ipc_message("response", request_id, method, 1, "Printer preset was not found");
            return;
        }

        wxGetApp().CallAfter([this, request_id, selection = *selection, method = std::string(method)]() {
            if (selection.make_active) {
                Tab* printer_tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
                if (printer_tab == nullptr || !printer_tab->select_preset(selection.preset_name)) {
                    send_ipc_message("response", request_id, method, 1, "Printer switch was cancelled");
                    sync_printers_to_webview();
                    return;
                }
            }

            if (!activate_anycubic_lan_bridge(selection.host)) {
                send_ipc_message("response", request_id, method, 1, "Could not connect the Device view to that printer");
                sync_printers_to_webview();
                return;
            }

            inject_bridge_token();
            send_ipc_message("response", request_id, method, 0, "success");
            sync_printers_to_webview();
        });
    }

    void send_ipc_message(const char* type, const std::string& request_id, const std::string& method, int code,
                          const std::string& message, const std::string& data_json = "{}")
    {
        if (browser() == nullptr)
            return;

        json body = json::object();
        body["type"] = type;
        if (!request_id.empty()) {
            body["id"] = request_id;
            body["sequence_id"] = request_id;
        }
        if (!method.empty()) {
            body["method"] = method;
            body["command"] = method;
        }

        json data = json::parse(data_json, nullptr, false);
        if (data.is_discarded())
            data = json::object();
        body["data"] = std::move(data);

        if (std::string(type) == "response") {
            body["code"] = code;
            body["message"] = message;
        }

        const wxString payload = wxString::FromUTF8(dump_json(body));
        const wxString script = "if (typeof HandleStudio === 'function') { HandleStudio(" + payload + "); } else { window.postMessage(" + payload + ", '*'); }";
        wxGetApp().CallAfter([this, script]() {
            if (browser() != nullptr)
                WebView::RunScript(browser(), script);
        });
    }

    void inject_bridge_token()
    {
        if (browser() == nullptr)
            return;
        const std::string token_json = json(anycubic_lan_bridge_token()).dump();
        const std::string host_json = json(anycubic_lan_bridge_host()).dump();
        const wxString script = wxString::FromUTF8(
            "window.__orcacubicBridgeToken=" + token_json + ";window.__orcacubicPrinterHost=" + host_json + ";");
        if (!WebView::RunScript(browser(), script))
            BOOST_LOG_TRIVIAL(warning) << "[AnycubicPrinterWebViewHandler] Could not update the active bridge token";
    }

    void inject_ac_localhost_interceptor()
    {
        if (browser() == nullptr) return;
        const wxString shim = R"(
        (function() {
            if (window._orcacubic_fetch_hooked) return;
            window._orcacubic_fetch_hooked = true;
            console.log('[OrcaCubic] Intercepting fetch for ac.localhost');
            const origFetch = window.fetch;
            window.fetch = async function(resource, init) {
                let urlStr = typeof resource === 'string' ? resource : (resource && resource.url ? resource.url : '');
                if (urlStr.includes('ac.localhost') || urlStr.includes('/api/v1/')) {
                    console.log('[OrcaCubic] Intercepted ac.localhost call:', urlStr);
                    if (urlStr.includes('/account/')) {
                        return new Response(JSON.stringify({ code: 200, data: { logged: true, name: "OrcaCubic User", token: "valid" }, message: "success" }), {
                            status: 200,
                            headers: { 'Content-Type': 'application/json' }
                        });
                    }
                    if (urlStr.includes('get_filaments') || urlStr.includes('filament')) {
                        return new Response(JSON.stringify({ code: 200, data: window.orcacubic_filaments || [], message: "success" }), {
                            status: 200,
                            headers: { 'Content-Type': 'application/json' }
                        });
                    }
                    return new Response(JSON.stringify({ code: 200, data: {}, message: "success" }), {
                        status: 200,
                        headers: { 'Content-Type': 'application/json' }
                    });
                }
                return origFetch.apply(this, arguments);
            };
        })();
        )";
        wxGetApp().CallAfter([this, shim]() {
            if (browser() != nullptr)
                WebView::RunScript(browser(), shim);
        });
    }

    void sync_filaments_to_webview(const std::string& request_id = "")
    {
        if (browser() == nullptr || wxGetApp().preset_bundle == nullptr)
            return;

        BOOST_LOG_TRIVIAL(info) << "[AnycubicPrinterWebViewHandler] sync_filaments_to_webview enter";

        auto full_cfg = wxGetApp().preset_bundle->full_config();
        const auto* colors             = full_cfg.option<ConfigOptionStrings>("filament_colour");
        const auto* types              = full_cfg.option<ConfigOptionStrings>("filament_type");
        const auto* vendors            = full_cfg.option<ConfigOptionStrings>("filament_vendor");
        const auto* names              = full_cfg.option<ConfigOptionStrings>("filament_settings_id");
        const auto* nozzle_temps       = full_cfg.option<ConfigOptionInts>("nozzle_temperature");
        const auto* nozzle_temps_init  = full_cfg.option<ConfigOptionInts>("nozzle_temperature_initial_layer");
        const auto* nozzle_range_lows  = full_cfg.option<ConfigOptionInts>("nozzle_temperature_range_low");
        const auto* nozzle_range_highs = full_cfg.option<ConfigOptionInts>("nozzle_temperature_range_high");
        const auto* bed_temps          = full_cfg.option<ConfigOptionInts>("bed_temperature");
        const auto* bed_temps_init     = full_cfg.option<ConfigOptionInts>("bed_temperature_initial_layer");
        const auto* densities          = full_cfg.option<ConfigOptionFloats>("filament_density");
        const auto* diameters          = full_cfg.option<ConfigOptionFloats>("filament_diameter");
        const auto* costs              = full_cfg.option<ConfigOptionFloats>("filament_cost");

        size_t count = colors ? colors->values.size() : 0;
        if (count == 0 && types)
            count = types->values.size();

        json filament_list = json::array();
        std::map<int, json> boxes_map;

        for (size_t i = 0; i < count; ++i) {
            std::string raw_color = (colors && i < colors->values.size()) ? colors->values[i] : "#FFFFFF";
            std::string clean_hex = format_hex_color(raw_color);

            int r = 175, g = 175, b = 175;
            parse_hex_color(clean_hex, r, g, b);

            std::string type   = (types && i < types->values.size()) ? types->values[i] : "PLA";
            std::string vendor = (vendors && i < vendors->values.size()) ? vendors->values[i] : "Anycubic";
            std::string name   = (names && i < names->values.size()) ? names->values[i] : (vendor + " " + type);

            int nozzle_temp = (nozzle_temps && i < nozzle_temps->values.size()) ? nozzle_temps->get_at(i) : 210;
            int nozzle_init = (nozzle_temps_init && i < nozzle_temps_init->values.size()) ? nozzle_temps_init->get_at(i) : nozzle_temp;
            int range_low   = (nozzle_range_lows && i < nozzle_range_lows->values.size()) ? nozzle_range_lows->get_at(i) : (nozzle_temp - 20);
            int range_high  = (nozzle_range_highs && i < nozzle_range_highs->values.size()) ? nozzle_range_highs->get_at(i) : (nozzle_temp + 20);
            int bed_temp    = (bed_temps && i < bed_temps->values.size()) ? bed_temps->get_at(i) : 60;
            int bed_init    = (bed_temps_init && i < bed_temps_init->values.size()) ? bed_temps_init->get_at(i) : bed_temp;

            double density  = (densities && i < densities->values.size()) ? densities->get_at(i) : 1.24;
            double diameter = (diameters && i < diameters->values.size()) ? diameters->get_at(i) : 1.75;
            double cost     = (costs && i < costs->values.size()) ? costs->get_at(i) : 0.0;

            int box_id   = static_cast<int>(i / 4);
            int box_slot = static_cast<int>(i % 4);

            json item = json::object();
            item["index"]                          = i + 1; // 1-based index
            item["slot_id"]                        = i;     // 0-based slot id
            item["box_id"]                         = box_id;
            item["box_slot"]                       = box_slot;
            item["color"]                          = clean_hex;
            item["color_rgb"]                      = {r, g, b};
            item["type"]                           = type;
            item["material"]                       = type;
            item["name"]                           = name;
            item["vendor"]                         = vendor;
            item["nozzle_temperature"]              = nozzle_temp;
            item["nozzle_temperature_initial_layer"] = nozzle_init;
            item["nozzle_temperature_range_low"]   = range_low;
            item["nozzle_temperature_range_high"]  = range_high;
            item["bed_temperature"]                 = bed_temp;
            item["bed_temperature_initial_layer"]  = bed_init;
            item["temperatures"] = {
                {"nozzle", nozzle_temp},
                {"nozzle_initial_layer", nozzle_init},
                {"nozzle_range_low", range_low},
                {"nozzle_range_high", range_high},
                {"bed", bed_temp},
                {"bed_initial_layer", bed_init}
            };
            item["density"]                        = density;
            item["diameter"]                       = diameter;
            item["cost"]                           = cost;
            item["loaded"]                         = true;

            filament_list.push_back(item);

            // Group slots by multi-color box for Anycubic ACE Pro
            json box_slot_obj = {
                {"index", box_slot},
                {"type", type},
                {"color", {r, g, b}},
                {"color_hex", clean_hex},
                {"target_temp", nozzle_temp},
                {"bed_temp", bed_temp}
            };
            if (boxes_map.find(box_id) == boxes_map.end()) {
                json box_obj = {
                    {"id", box_id},
                    {"slots", json::array()}
                };
                boxes_map[box_id] = box_obj;
            }
            boxes_map[box_id]["slots"].push_back(box_slot_obj);
        }

        json multi_color_boxes = json::array();
        for (auto& pair : boxes_map) {
            multi_color_boxes.push_back(pair.second);
        }

        json payload = {
            {"type", "orcacubic_filament_sync"},
            {"command", "sync_filaments"},
            {"filaments", filament_list},
            {"multi_color_box", multi_color_boxes},
            {"data", {
                {"filaments", filament_list},
                {"multi_color_box", multi_color_boxes}
            }}
        };
        if (!request_id.empty()) {
            payload["sequence_id"] = request_id;
            payload["id"] = request_id;
        }

        const wxString payload_str = wxString::FromUTF8(dump_json(payload));
        const wxString script = wxString::Format(
            "if (typeof HandleStudio === 'function') { HandleStudio(%s); } else { window.postMessage(%s, '*'); }"
            "window.orcacubic_filaments = %s.filaments;"
            "window.orcacubic_multi_color_box = %s.multi_color_box;"
            "try { window.dispatchEvent(new CustomEvent('orcacubic_filament_sync', { detail: %s })); } catch(e) {}",
            payload_str, payload_str, payload_str, payload_str, payload_str);

        BOOST_LOG_TRIVIAL(info) << "[AnycubicPrinterWebViewHandler] Synced " << count << " filaments to webview";

        wxGetApp().CallAfter([this, script]() {
            if (browser() != nullptr)
                WebView::RunScript(browser(), script);
        });
    }

    void sync_login_info_to_webview(const std::string& request_id = "")
    {
        if (browser() == nullptr)
            return;

        const bool logged_in = wxGetApp().is_user_login();
        json body = json::object();
        if (!request_id.empty()) {
            body["sequence_id"] = request_id;
            body["id"] = request_id;
        }

        if (logged_in) {
            body["command"] = "studio_userlogin";
            json data = json::object();
            data["avatar"] = "";
            data["name"]   = "Anycubic User";
            data["token"]  = "";
            data["logged"] = true;
            body["data"]   = data;
        } else {
            body["command"] = "studio_useroffline";
            json data = json::object();
            data["logged"] = false;
            body["data"]   = data;
        }

        const wxString payload = wxString::FromUTF8(dump_json(body));
        const wxString script = wxString::Format(
            "if (typeof HandleStudio === 'function') { HandleStudio(%s); } else { window.postMessage(%s, '*'); }"
            "if (typeof window.status_notify === 'function') { try { window.status_notify(%d, '%s'); } catch(e) {} }",
            payload, payload, logged_in ? 1 : 0, logged_in ? "{\"status\":1}" : "");

        wxGetApp().CallAfter([this, script]() {
            if (browser() != nullptr)
                WebView::RunScript(browser(), script);
        });
    }

    void handle_print_request(const std::string& request_id, const std::string& method, const json& params)
    {
        if (upload_in_progress.exchange(true)) {
            send_ipc_message("response", request_id, method, 1, "Upload/print already in progress");
            return;
        }

        if (upload_thread.joinable())
            upload_thread.join();

        std::string file_path = json_string(params, "filePath");
        if (file_path.empty())
            file_path = json_string(params, "file_path");
        if (file_path.empty())
            file_path = json_string(params, "path");

        std::string file_name = json_string(params, "fileName");
        if (file_name.empty())
            file_name = json_string(params, "filename");

        // If no file path was given, resolve from current sliced plate in plater
        if (file_path.empty() && wxGetApp().plater()) {
            auto& plate_list = wxGetApp().plater()->get_partplate_list();
            int curr_idx = plate_list.get_curr_plate_index();
            if (curr_idx >= 0 && curr_idx < static_cast<int>(plate_list.get_plate_list().size())) {
                PartPlate* plate = plate_list.get_curr_plate();
                if (plate && plate->is_slice_result_valid()) {
                    std::string tmp = plate->get_tmp_gcode_path();
                    if (!tmp.empty() && boost::filesystem::exists(path_from_utf8(tmp))) {
                        file_path = tmp;
                    }
                }
            }
        }

        if (file_path.empty()) {
            upload_in_progress = false;
            send_ipc_message("response", request_id, method, 1, "No print file specified and plate is not sliced");
            return;
        }

        boost::filesystem::path source_path = path_from_utf8(file_path);
        if (!boost::filesystem::exists(source_path)) {
            upload_in_progress = false;
            send_ipc_message("response", request_id, method, 1, "Print file does not exist: " + file_path);
            return;
        }

        if (file_name.empty())
            file_name = filename_to_utf8(source_path);

        DynamicPrintConfig* config = get_active_printer_config();
        std::unique_ptr<PrintHost> print_host(config == nullptr ? nullptr : PrintHost::get_print_host(config));
        if (print_host == nullptr) {
            upload_in_progress = false;
            send_ipc_message("response", request_id, method, 1, "Could not get a valid Printer Host reference");
            return;
        }

        bool auto_start = true;
        if (method == "upload_file" && params.contains("auto_start") && params["auto_start"].is_boolean()) {
            auto_start = params["auto_start"].get<bool>();
        }

        stop_upload = false;
        upload_thread = std::thread([this, request_id, method, file_path, file_name, source_path, auto_start, print_host = std::move(print_host)]() mutable {
            std::string error_message;

            PrintHostUpload upload_data;
            upload_data.use_3mf     = false;
            upload_data.post_action = auto_start ? PrintHostPostUploadAction::StartPrint : PrintHostPostUploadAction::None;
            upload_data.source_path = source_path;
            upload_data.upload_path = path_from_utf8(file_name);

            const bool success = print_host->upload(
                std::move(upload_data),
                [this, request_id](Http::Progress progress, bool& cancel) {
                    cancel = stop_upload.load();
                    json data = {
                        {"uploadedBytes", static_cast<uint64_t>(progress.ulnow)},
                        {"totalBytes", static_cast<uint64_t>(progress.ultotal)},
                        {"progress", progress.ultotal > 0 ? (progress.ulnow * 100 / progress.ultotal) : 0}
                    };
                    send_ipc_message("event", request_id, "upload_progress", 0, "", dump_json(data));
                },
                [&error_message](wxString error) {
                    error_message = error.ToUTF8().data();
                },
                [this, request_id](wxString tag, wxString status) {
                    json data = {
                        {"tag", tag.ToUTF8().data()},
                        {"status", status.ToUTF8().data()}
                    };
                    send_ipc_message("event", request_id, "upload_info", 0, "", dump_json(data));
                });

            upload_in_progress = false;

            if (success) {
                json data = {
                    {"success", true},
                    {"filePath", file_path},
                    {"fileName", file_name},
                    {"printed", auto_start}
                };
                send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
            } else {
                if (error_message.empty())
                    error_message = "Upload/Print failed";
                send_ipc_message("response", request_id, method, 1, error_message);
            }
        });
    }

    void handle_open_project_request(const std::string& request_id, const std::string& method, const json& params)
    {
        std::string path = json_string(params, "path");
        if (path.empty())
            path = json_string(params, "file_path");
        if (path.empty())
            path = json_string(params, "file");
        std::string project_id = json_string(params, "project_id");
        if (project_id.empty())
            project_id = json_string(params, "id");
        std::string url = json_string(params, "url");

        if (!path.empty()) {
            boost::filesystem::path fs_path = path_from_utf8(path);
            if (boost::filesystem::exists(fs_path)) {
                wxArrayString files;
                files.Add(wxString::FromUTF8(path));
                wxGetApp().CallAfter([files]() {
                    if (wxGetApp().plater()) {
                        wxGetApp().plater()->load_files(files);
                    }
                });
                send_ipc_message("response", request_id, method, 0, "success");
                return;
            }
        }

        if (!project_id.empty()) {
            wxGetApp().CallAfter([project_id]() {
                wxGetApp().request_open_project(project_id);
            });
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        if (!url.empty()) {
            wxGetApp().CallAfter([url]() {
                wxGetApp().request_open_project(url);
            });
            send_ipc_message("response", request_id, method, 0, "success");
            return;
        }

        send_ipc_message("response", request_id, method, 1, "Missing project_id, path, or url");
    }

    void handle_open_file_dialog_request(const std::string& request_id, const std::string& method, const std::string& params_json)
    {
        json params = json::parse(params_json, nullptr, false);
        if (params.is_discarded())
            params = json::object();

        const std::string filter = json_string(params, "filter").empty() ? "All files (*.*)|*.*" : json_string(params, "filter");

        wxWindow* parent = owner().GetParent();
        if (parent == nullptr)
            parent = wxGetApp().GetTopWindow();

        wxFileDialog open_file_dialog(parent, _L("Open File"), "", "", wxString::FromUTF8(filter), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        json data = json::object();
        data["files"] = json::array();
        if (open_file_dialog.ShowModal() != wxID_CANCEL)
            data["files"].push_back(open_file_dialog.GetPath().ToUTF8().data());

        send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
    }

    void handle_get_sn_request(const std::string& request_id, const std::string& method)
    {
        std::string sn;
        if (DynamicPrintConfig* config = get_active_printer_config()) {
            const std::unique_ptr<PrintHost> host(PrintHost::get_print_host(config));
            if (host)
                sn = host->get_sn();
        }
        json data = { { "sn", sn } };
        send_ipc_message("response", request_id, method, 0, "success", dump_json(data));
    }

    std::atomic<bool> upload_in_progress { false };
    std::atomic<bool> stop_upload { false };
    std::thread       upload_thread;
};

} // namespace

std::unique_ptr<PrinterWebViewHandler> create_printer_webview_handler(PrinterWebView& owner)
{
    auto     cfg = get_active_printer_config();
    if(cfg == nullptr) return nullptr;
    
    const auto host_type = cfg->option<ConfigOptionEnum<PrintHostType>>("host_type")->value;
    switch (host_type)
    {
        case PrintHostType::htElegooLink:
            return std::make_unique<ElegooPrinterWebViewHandler>(owner);
        case PrintHostType::htAnycubic:
            return std::make_unique<AnycubicPrinterWebViewHandler>(owner);
        default:
            return nullptr;
    }
}

} // GUI
} // Slic3r