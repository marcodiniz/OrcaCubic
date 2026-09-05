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
        {1, -1, "PLA", "#23a3c7", true, 0.f},
        {2, -1, "PLA", "#75787b", true, 0.f},
        {3, -1, "PLA", "#fddb27", true, 0.f},
        {4, -1, "PLA", "#ff8da1", true, 0.f},
    };

    const auto mapping = Slic3r::build_anycubic_ams_mapping(tools, slots, {1, 4});

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
        {1, -1, "PLA", "#23a3c7", true, 0.f},
        {2, -1, "PLA", "#75787b", true, 0.f},
    };

    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {1}).empty());
    CHECK(Slic3r::build_anycubic_ams_mapping(tools, slots, {1, 2}).empty());
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
