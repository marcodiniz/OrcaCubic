#include "PrintHostDialogs.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "BitmapComboBox.hpp"
#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/AppConfig.hpp"

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace Slic3r {
namespace GUI {
namespace {

wxColour anycubic_contrasting_text(const wxColour& background)
{
    return background.GetLuminance() < 0.60 ? *wxWHITE : wxColour("#303030");
}

long long anycubic_color_distance(const wxColour& lhs, const wxColour& rhs)
{
    const long long dr = static_cast<long long>(lhs.Red()) - static_cast<long long>(rhs.Red());
    const long long dg = static_cast<long long>(lhs.Green()) - static_cast<long long>(rhs.Green());
    const long long db = static_cast<long long>(lhs.Blue()) - static_cast<long long>(rhs.Blue());
    return dr * dr + dg * dg + db * db;
}

} // namespace

AnycubicPrintHostSendDialog::AnycubicPrintHostSendDialog(const fs::path& path,
                                                         PrintHostPostUploadActions post_actions,
                                                         const wxArrayString& groups,
                                                         const wxArrayString& storage_paths,
                                                         const wxArrayString& storage_names,
                                                         bool switch_to_device_tab,
                                                         const Slic3r::AnycubicLink* host,
                                                         std::vector<Slic3r::AnycubicMaterialSlot> slots,
                                                         std::vector<Slic3r::AnycubicToolFilament> project_filaments)
    : PrintHostSendDialog(path, post_actions, groups, storage_paths, storage_names, switch_to_device_tab)
    , m_host(host)
    , m_slots(std::move(slots))
    , m_project_filaments(std::move(project_filaments))
{
}

void AnycubicPrintHostSendDialog::init()
{
    AppConfig* app_config = wxGetApp().app_config;
    auto load_bool = [app_config](const char* key, bool fallback) {
        const std::string value = app_config->get("recent", key);
        return value.empty() ? fallback : value == "1";
    };
    m_auto_leveling = load_bool(CONFIG_KEY_LEVELING, true);
    m_resonance_compensation = load_bool(CONFIG_KEY_RESONANCE, false);
    m_flow_calibration = load_bool(CONFIG_KEY_FLOW, false);
    m_timelapse = load_bool(CONFIG_KEY_TIMELAPSE, false);

    SetTitle(_L("Remote Print"));
    SetMinSize(wxSize(FromDIP(620), FromDIP(520)));

    wxString recent_path = from_u8(app_config->get("recent", "printhost_path"));
    if (!recent_path.empty() && recent_path.Last() != '/')
        recent_path += '/';
    recent_path += m_path.filename().wstring();
    txt_filename->SetValue(recent_path);
    content_sizer->Add(txt_filename, 0, wxEXPAND);
    content_sizer->AddSpacer(FromDIP(12));

    auto* mapping_title = new wxStaticText(this, wxID_ANY, _L("Color Mapping"));
    mapping_title->SetFont(::Label::Head_13);
    content_sizer->Add(mapping_title, 0, wxBOTTOM, FromDIP(8));

    if (m_project_filaments.empty()) {
        content_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Slice the plate first to map project colors.")));
    } else {
        for (const auto& tool : m_project_filaments) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* source = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(78), FromDIP(42)));
            source->SetMinSize(wxSize(FromDIP(78), FromDIP(42)));
            source->SetBackgroundColour(wxColour(from_u8(tool.color)));
            auto* source_label = new wxStaticText(source, wxID_ANY, wxString::Format("%s  T%d", from_u8(tool.type), tool.tool_id));
            source_label->SetForegroundColour(anycubic_contrasting_text(source->GetBackgroundColour()));
            auto* source_sizer = new wxBoxSizer(wxVERTICAL);
            source_sizer->AddStretchSpacer();
            source_sizer->Add(source_label, 0, wxALIGN_CENTER_HORIZONTAL);
            source_sizer->AddStretchSpacer();
            source->SetSizer(source_sizer);
            row->Add(source, 0, wxRIGHT, FromDIP(14));
            row->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xe2\x86\x92")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));

            auto* combo = new BitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(250), -1), 0, nullptr, wxCB_READONLY);
            for (const auto& slot : m_slots) {
                wxBitmap* icon = get_extruder_color_icon(slot.color, "", FromDIP(16), FromDIP(16));
                combo->Append(wxString::Format(_L("ACE Slot %d - %s"), slot.slot_id, from_u8(slot.type)), icon ? *icon : wxNullBitmap);
            }
            row->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
            content_sizer->Add(row, 0, wxBOTTOM, FromDIP(8));
            m_slot_combos.push_back(combo);
        }
        auto_assign_mappings();
    }

    content_sizer->AddSpacer(FromDIP(10));
    auto* calibration_title = new wxStaticText(this, wxID_ANY, _L("Calibration"));
    calibration_title->SetFont(::Label::Head_13);
    content_sizer->Add(calibration_title, 0, wxBOTTOM, FromDIP(6));

    auto add_toggle = [this](const wxString& label, const wxString& tooltip, bool& value) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* checkbox = new ::CheckBox(this);
        checkbox->SetValue(value);
        checkbox->SetToolTip(tooltip);
        checkbox->Bind(wxEVT_TOGGLEBUTTON, [&value](wxCommandEvent& event) {
            value = event.IsChecked();
            event.Skip(); // Allow CheckBox's own handler to redraw the checked/unchecked bitmap.
        });
        auto* text = new wxStaticText(this, wxID_ANY, label);
        text->SetToolTip(tooltip);
        row->Add(checkbox, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
        row->Add(text, 0, wxALIGN_CENTER_VERTICAL);
        content_sizer->Add(row, 0, wxBOTTOM, FromDIP(6));
    };
    add_toggle(_L("Auto Leveling"), _L("Probe and compensate the build plate before this print."), m_auto_leveling);
    add_toggle(_L("Resonance Compensation"), _L("Run vibration compensation before this print."), m_resonance_compensation);
    add_toggle(_L("Flow Calibration"), _L("Calibrate extrusion flow before this print."), m_flow_calibration);
    add_toggle(_L("Time-lapse"), _L("Capture a time-lapse while printing. The camera must be available."), m_timelapse);

    auto* start = add_button(wxID_YES, true, _L("Start Print"));
    start->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (validate_before_close()) {
            post_upload_action = PrintHostPostUploadAction::StartPrint;
            EndDialog(wxID_OK);
        }
    });
    add_button(wxID_CANCEL, false, _L("Cancel"));
    finalize();
    CenterOnParent();
}

void AnycubicPrintHostSendDialog::auto_assign_mappings()
{
    for (size_t tool_idx = 0; tool_idx < m_project_filaments.size() && tool_idx < m_slot_combos.size(); ++tool_idx) {
        int best = wxNOT_FOUND;
        long long best_distance = std::numeric_limits<long long>::max();
        const wxColour project_color(from_u8(m_project_filaments[tool_idx].color));
        for (size_t slot_idx = 0; slot_idx < m_slots.size(); ++slot_idx) {
            if (!slot_matches_tool(m_slots[slot_idx], m_project_filaments[tool_idx]))
                continue;
            const long long distance = anycubic_color_distance(project_color, wxColour(from_u8(m_slots[slot_idx].color)));
            if (best == wxNOT_FOUND || distance < best_distance) {
                best = static_cast<int>(slot_idx);
                best_distance = distance;
            }
        }
        m_slot_combos[tool_idx]->SetSelection(best);
    }
}

bool AnycubicPrintHostSendDialog::slot_matches_tool(const AnycubicMaterialSlot& slot, const AnycubicToolFilament& tool) const
{
    auto normalize = [](std::string value) {
        boost::to_upper(value);
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isalnum(ch); }), value.end());
        return value;
    };
    return slot.loaded && normalize(slot.type) == normalize(tool.type);
}

bool AnycubicPrintHostSendDialog::validate_before_close()
{
    if (m_project_filaments.empty()) {
        show_error(this, _L("Slice the plate before starting a remote print."));
        return false;
    }
    if (m_slot_combos.size() != m_project_filaments.size()) {
        show_error(this, _L("The color mapping could not be created."));
        return false;
    }
    for (size_t idx = 0; idx < m_slot_combos.size(); ++idx) {
        const int selection = m_slot_combos[idx]->GetSelection();
        if (selection == wxNOT_FOUND || selection >= static_cast<int>(m_slots.size()) || !slot_matches_tool(m_slots[selection], m_project_filaments[idx])) {
            show_error(this, _L("Each project color must be mapped to an ACE slot containing the same material type."));
            return false;
        }
    }
    return true;
}

std::map<std::string, std::string> AnycubicPrintHostSendDialog::extendedInfo() const
{
    std::vector<int> selections;
    for (const auto* combo : m_slot_combos) {
        const int selected = combo->GetSelection();
        if (selected < 0 || selected >= static_cast<int>(m_slots.size()))
            return {};
        selections.push_back(m_slots[selected].slot_id);
    }

    json mapping = json::array();
    for (const auto& entry : build_anycubic_ams_mapping(m_project_filaments, m_slots, selections)) {
        auto color_array = [](const std::string& raw) {
            wxColour color(from_u8(raw));
            return json::array({color.Red(), color.Green(), color.Blue()});
        };
        mapping.push_back({
            {"ams_index", entry.ams_index},
            {"paint_index", entry.paint_index},
            {"material_type", entry.material_type},
            {"ams_color", color_array(entry.ams_color)},
            {"paint_color", color_array(entry.paint_color)}
        });
    }

    return {
        {"ams_mapping", mapping.dump()},
        {"auto_leveling", m_auto_leveling ? "1" : "0"},
        {"vibration_compensation", m_resonance_compensation ? "1" : "0"},
        {"flow_calibration", m_flow_calibration ? "1" : "0"},
        {"timelapse", m_timelapse ? "1" : "0"}
    };
}

void AnycubicPrintHostSendDialog::EndModal(int ret)
{
    if (ret == wxID_OK) {
        AppConfig* config = wxGetApp().app_config;
        config->set("recent", CONFIG_KEY_LEVELING, m_auto_leveling ? "1" : "0");
        config->set("recent", CONFIG_KEY_RESONANCE, m_resonance_compensation ? "1" : "0");
        config->set("recent", CONFIG_KEY_FLOW, m_flow_calibration ? "1" : "0");
        config->set("recent", CONFIG_KEY_TIMELAPSE, m_timelapse ? "1" : "0");
    }
    PrintHostSendDialog::EndModal(ret);
}

} // namespace GUI
} // namespace Slic3r
