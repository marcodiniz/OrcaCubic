#include <catch2/catch_test_macros.hpp>

#include "slic3r/Utils/AnycubicLink.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "test_utils.hpp"

#include <miniz.h>

#include <fstream>

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

TEST_CASE("Anycubic remote print mapping normalizes vendor preset names like Generic PLA to PLA", "[anycubic][remote-print]")
{
    const std::vector<AnycubicToolFilament> tools {
        {0, "Generic PLA", "#23a3c7"},
    };
    const std::vector<AnycubicMaterialSlot> slots {
        {0, -1, 0, "rack", "PLA", "#23a3c7", true, 0.f},
        {1, -1, 1, "rack", "PLA", "#75787b", true, 0.f},
        {2, -1, 2, "rack", "PLA", "#fddb27", true, 0.f},
        {3, -1, 3, "rack", "PLA", "#d7d9d2", true, 0.f},
    };

    // User maps Filament 1 (Generic PLA, tool 0) -> Slot 3 (PLA, slot_id 2)
    const auto mapping = Slic3r::build_anycubic_ams_mapping(tools, slots, {2});
    REQUIRE(mapping.size() == 1);
    CHECK(mapping[0].paint_index == 0);
    CHECK(mapping[0].ams_index == 2);
}

TEST_CASE("Anycubic remote print mapping supports Kobra X native built-in rack slots", "[anycubic][remote-print]")
{
    const std::vector<AnycubicToolFilament> tools {
        {0, "PLA", "#23a3c7"},
        {1, "PLA", "#75787b"},
    };
    const std::vector<AnycubicMaterialSlot> slots {
        {0, -1, 0, "rack", "PLA", "#23a3c7", true, 0.f},
        {1, -1, 1, "rack", "PLA", "#75787b", true, 0.f},
        {2, -1, 2, "rack", "PLA", "#fddb27", true, 0.f},
        {3, -1, 3, "rack", "PLA", "#d7d9d2", true, 0.f},
    };

    const auto mapping = Slic3r::build_anycubic_ams_mapping(tools, slots, {0, 1});
    REQUIRE(mapping.size() == 2);
    CHECK(mapping[0].ams_index == 0);
    CHECK(mapping[0].paint_index == 0);
    CHECK(mapping[1].ams_index == 1);
    CHECK(mapping[1].paint_index == 1);
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

TEST_CASE("Anycubic replaces initial flush block and preserves subsequent changes", "[anycubic][toolchange]")
{
    std::string sample_gcode =
        "G90\n"
        "G9111 bedTemp=60 extruderTemp=215\n"
        "M83\n"
        "; FLUSH_START\n"
        ";;; G1 X277.5 F600\n"
        ";;; G1 E-33 F600\n"
        "T0\n"
        ";;; G1 E8 F300\n"
        ";;; M400 P3643\n"
        "; FLUSH_END\n"
        "G1 X10 Y10 E1\n"
        "; FLUSH_START\n"
        "T2 ; color change layer 50\n"
        ";;; G1 X277.5 F600\n"
        "; FLUSH_END\n"
        "G1 X20 Y20 E1\n";

    std::string modified;
    // Default script
    bool replaced = Slic3r::skip_first_toolchange_in_gcode(sample_gcode, modified);

    REQUIRE(replaced);
    CHECK(modified.find("Replace first tool change (T0) with initial purge at purge box") != std::string::npos);
    CHECK(modified.find("G28 X\n") != std::string::npos);
    CHECK(modified.find("G1 E12 F300") != std::string::npos);
    CHECK(modified.find("G1 E-33") == std::string::npos);   // No 33mm retract in first block
    // The cutter move G1 X277.5 was removed from the first block, and only appears in the preserved second block
    CHECK(modified.find("G1 X277.5") > modified.find("T2"));
    // Subsequent flush block for T2 on later layer must NOT be replaced
    CHECK(modified.find("T2 ; color change layer 50") != std::string::npos);

    // Custom script check
    std::string custom_script = "M83\nG1 X-15.8 F6000\nG1 E8 F250\nM400\nG28 X";
    std::string modified_custom;
    REQUIRE(Slic3r::skip_first_toolchange_in_gcode(sample_gcode, modified_custom, custom_script));
    CHECK(modified_custom.find("G1 E8 F250") != std::string::npos);
    CHECK(modified_custom.find("G1 X-15.8 F6000") != std::string::npos);
}

TEST_CASE("Anycubic replaces standalone initial tool command without flush markers", "[anycubic][toolchange]")
{
    const std::string sample_gcode =
        "G28\n"
        "  T3\n"
        "G1 Z0.2\n"
        "T1 ; second tool later\n";

    std::string modified;
    const bool replaced = Slic3r::skip_first_toolchange_in_gcode(sample_gcode, modified);

    REQUIRE(replaced);
    CHECK(modified.find("Replace first tool change (T3) with initial purge at purge box") != std::string::npos);
    CHECK(modified.find("G28 X\n") != std::string::npos);
    CHECK(modified.find("G1 E12 F300") != std::string::npos);
    // Subsequent tool change must remain untouched
    CHECK(modified.find("T1 ; second tool later") != std::string::npos);
}

TEST_CASE("Anycubic leaves G-code unchanged when no tool command exists", "[anycubic][toolchange]")
{
    const std::string no_tool =
        "G28\n"
        "; T0 in comment only\n"
        "G1 Z0.2\n";
    std::string output;
    CHECK_FALSE(Slic3r::skip_first_toolchange_in_gcode(no_tool, output));
    CHECK(output == no_tool);
}

TEST_CASE("Anycubic 3MF first toolchange replacement preserves a readable archive", "[anycubic][toolchange]")
{
    ScopedTemporaryDir temp_dir("orcacubic-3mf");
    const boost::filesystem::path source = temp_dir.path() / "source.gcode.3mf";
    boost::filesystem::path output;
    const std::string gcode =
        "G28\n"
        "; FLUSH_START\n"
        ";;; G1 E-33 F600\n"
        "T3\n"
        ";;; G1 E8 F300\n"
        ";;; G1 E13 F1200\n"
        "; FLUSH_END\n"
        "G1 X10 Y10 E1\n";
    const std::string metadata = "<metadata>preserved</metadata>";

    mz_zip_archive writer;
    mz_zip_zero_struct(&writer);
    REQUIRE(mz_zip_writer_init_file(&writer, source.string().c_str(), 0));
    std::string old_md5 = "OLDMD5PLACEHOLDER";
    REQUIRE(mz_zip_writer_add_mem(&writer, "Metadata/plate_1.gcode.md5", old_md5.data(), old_md5.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_add_mem(&writer, "Metadata/plate_1.gcode", gcode.data(), gcode.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_add_mem(&writer, "Metadata/slice_info.config", metadata.data(), metadata.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&writer));
    REQUIRE(mz_zip_writer_end(&writer));

    std::string error;
    REQUIRE(Slic3r::process_gcode_to_skip_first_toolchange(source, output, error, "M83\nG28 X\nG1 E12 F300"));

    mz_zip_archive reader;
    mz_zip_zero_struct(&reader);
    REQUIRE(mz_zip_reader_init_file(&reader, output.string().c_str(), 0));
    size_t gcode_size = 0;
    void* gcode_data = mz_zip_reader_extract_file_to_heap(&reader, "Metadata/plate_1.gcode", &gcode_size, 0);
    REQUIRE(gcode_data != nullptr);
    const std::string processed(static_cast<const char*>(gcode_data), gcode_size);
    free(gcode_data);
    size_t metadata_size = 0;
    void* metadata_data = mz_zip_reader_extract_file_to_heap(&reader, "Metadata/slice_info.config", &metadata_size, 0);
    REQUIRE(metadata_data != nullptr);
    const std::string preserved_metadata(static_cast<const char*>(metadata_data), metadata_size);
    free(metadata_data);
    size_t md5_size = 0;
    void* md5_data = mz_zip_reader_extract_file_to_heap(&reader, "Metadata/plate_1.gcode.md5", &md5_size, 0);
    REQUIRE(md5_data != nullptr);
    const std::string updated_md5(static_cast<const char*>(md5_data), md5_size);
    free(md5_data);
    REQUIRE(mz_zip_reader_end(&reader));

    CHECK(processed.find("Replace first tool change (T3) with initial purge at purge box") != std::string::npos);
    CHECK(processed.find("G28 X\n") != std::string::npos);
    CHECK(processed.find("G1 E12 F300") != std::string::npos);
    CHECK(processed.find("G1 E-33") == std::string::npos);
    CHECK(preserved_metadata == metadata);
    CHECK(updated_md5 != old_md5);
    CHECK(updated_md5.size() == 32);
}
