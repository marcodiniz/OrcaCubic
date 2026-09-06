#include <catch2/catch_test_macros.hpp>

#include "slic3r/Utils/AnycubicLink.hpp"

using Slic3r::AnycubicMaterialSlot;
using Slic3r::AnycubicPrintSettings;
using Slic3r::AnycubicToolFilament;

TEST_CASE("Anycubic remote print mapping preserves explicitly selected ACE slots", "[anycubic][remote-print]")
{
    const std::vector<AnycubicToolFilament> tools {
        {0, "PLA", "#23a3c7"},
        {3, "PLA", "#ff8da1"},
    };
    const std::vector<AnycubicMaterialSlot> slots {
        {0, 0, 0, "ace", "PLA", "#23a3c7", true, 0.f},
        {1, 0, 1, "ace", "PLA", "#75787b", true, 0.f},
        {2, 0, 2, "ace", "PLA", "#fddb27", true, 0.f},
        {3, 0, 3, "ace", "PLA", "#ff8da1", true, 0.f},
    };

    const auto mapping = Slic3r::build_anycubic_ams_mapping(tools, slots, {0, 3});

    REQUIRE(mapping.size() == 2);
    CHECK(mapping[0].paint_index == 0);
    CHECK(mapping[0].ams_index == 0);
    CHECK(mapping[1].paint_index == 3);
    CHECK(mapping[1].ams_index == 3);
}

TEST_CASE("Anycubic remote print mapping rejects invalid or material-incompatible selections", "[anycubic][remote-print]")
{
    const std::vector<AnycubicToolFilament> tools {
        {0, "PLA", "#23a3c7"},
        {1, "PETG", "#10b981"},
    };
    const std::vector<AnycubicMaterialSlot> slots {
        {0, 0, 0, "ace", "PLA", "#23a3c7", true, 0.f},
        {1, 0, 1, "ace", "PLA", "#75787b", true, 0.f},
    };

    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {1}).empty());
    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {0, 1}).empty());
}

TEST_CASE("Anycubic remote print mapping supports a second ACE unit", "[anycubic][remote-print][ace]")
{
    const std::vector<AnycubicToolFilament> tools {{0, "PLA", "#ffffff"}, {1, "PETG", "#000000"}};
    const std::vector<AnycubicMaterialSlot> slots {
        {0, 0, 0, "ace", "PLA", "#ffffff", true, 0.f},
        {6, 1, 2, "ace", "PETG", "#000000", true, 0.f},
    };
    const auto mapping = Slic3r::build_anycubic_ams_mapping(tools, slots, {0, 6});
    REQUIRE(mapping.size() == 2);
    CHECK(mapping[0].ams_index == 0);
    CHECK(mapping[1].ams_index == 6);
    CHECK(mapping[1].paint_index == 1);
}

TEST_CASE("Anycubic ACE mapping does not treat the external spool as an ACE slot", "[anycubic][remote-print][ace]")
{
    const std::vector<AnycubicToolFilament> tools {{0, "TPU", "#070809"}};
    const std::vector<AnycubicMaterialSlot> slots {{-1, -1, 0, "external", "TPU", "#070809", true, 0.f}};
    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {-1}).empty());
}

TEST_CASE("Anycubic ACE mapping does not treat a mixed-mode external rack as an ACE slot", "[anycubic][remote-print][ace]")
{
    const std::vector<AnycubicToolFilament> tools {{0, "TPU", "#070809"}};
    const std::vector<AnycubicMaterialSlot> slots {{-1, -1, 0, "external_mcb", "TPU", "#070809", true, 0.f}};
    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {-1}).empty());
}

TEST_CASE("Anycubic task settings reflect remote print calibration toggles", "[anycubic][remote-print]")
{
    AnycubicPrintSettings settings;
    settings.auto_leveling = false;
    settings.vibration_compensation = true;
    settings.flow_calibration = true;
    settings.timelapse = true;

    const auto task = Slic3r::build_anycubic_task_settings(settings);

    CHECK(task.auto_leveling == 0);
    CHECK(task.vibration_compensation == 1);
    CHECK(task.flow_calibration == 1);
    CHECK(task.timelapse_status == 1);
}


TEST_CASE("Anycubic printer list contains only configured Anycubic printers", "[anycubic][multi-printer]")
{
    std::vector<Slic3r::AnycubicPrinterCandidate> candidates {
        {"Workshop Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.10", true},
        {"Empty Anycubic preset", "Anycubic Kobra X 0.4 nozzle", "anycubic", "", false},
        {"OctoPrint printer", "Generic Marlin", "octoprint", "192.0.2.11", false},
        {"Second Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "http://192.0.2.12", false},
    };

    const auto printers = Slic3r::build_anycubic_printer_list(candidates, "192.0.2.12");

    REQUIRE(printers.size() == 2);
    CHECK(printers[0].preset_name == "Workshop Kobra X");
    CHECK(printers[0].host == "192.0.2.10");
    CHECK(printers[0].selected);
    CHECK(printers[1].preset_name == "Second Kobra X");
    CHECK(printers[1].host == "192.0.2.12");
    CHECK_FALSE(printers[1].selected);
}

TEST_CASE("Anycubic printer switching matches exact preset names", "[anycubic][multi-printer]")
{
    const std::vector<Slic3r::AnycubicPrinterCandidate> candidates {
        {"Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.20", true},
        {"Kobra X Backup", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.21", false},
    };

    CHECK(Slic3r::find_anycubic_printer_candidate(candidates, "Kobra X Backup") == 1);
    CHECK(Slic3r::find_anycubic_printer_candidate(candidates, "kobra x backup") == -1);
    CHECK(Slic3r::find_anycubic_printer_candidate(candidates, "Kobra X Backup ") == -1);
}

TEST_CASE("Anycubic printer list distinguishes active slicer printer from monitored Device printer", "[anycubic][multi-printer]")
{
    std::vector<Slic3r::AnycubicPrinterCandidate> candidates {
        {"Workshop Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.10", true},
        {"Second Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.12", false},
    };

    const auto printers = Slic3r::build_anycubic_printer_list(candidates, "192.0.2.10", "192.0.2.12");

    REQUIRE(printers.size() == 2);
    CHECK(printers[0].active);
    CHECK_FALSE(printers[0].monitored);
    CHECK(printers[0].selected);
    CHECK_FALSE(printers[1].active);
    CHECK(printers[1].monitored);
    CHECK_FALSE(printers[1].selected);
}

TEST_CASE("Anycubic printer selection can monitor without changing the active printer", "[anycubic][multi-printer]")
{
    std::vector<Slic3r::AnycubicPrinterCandidate> candidates {
        {"Workshop Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.10", true},
        {"Second Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.12", false},
    };

    const auto selection = Slic3r::choose_anycubic_printer(candidates, "Second Kobra X", false);

    REQUIRE(selection.has_value());
    CHECK(selection->preset_name == "Second Kobra X");
    CHECK(selection->host == "192.0.2.12");
    CHECK_FALSE(selection->make_active);
}

TEST_CASE("Anycubic printer selection requires an explicit Make Active action", "[anycubic][multi-printer]")
{
    std::vector<Slic3r::AnycubicPrinterCandidate> candidates {
        {"Workshop Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "192.0.2.10", true},
        {"Second Kobra X", "Anycubic Kobra X 0.4 nozzle", "anycubic", "http://192.0.2.12/", false},
    };

    const auto selection = Slic3r::choose_anycubic_printer(candidates, "Second Kobra X", true);

    REQUIRE(selection.has_value());
    CHECK(selection->host == "192.0.2.12");
    CHECK(selection->make_active);
    CHECK_FALSE(Slic3r::choose_anycubic_printer(candidates, "Missing printer", true).has_value());
}
