"""Contract for the opt-in S1 Rinkhals beta profile (no hardware access)."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / "resources/profiles"
MODEL = "Anycubic Kobra S1 (Rinkhals)"
MACHINE = MODEL + " 0.4 nozzle"


def load(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8-sig"))


def resolve(relative):
    profile = load(relative)
    if "inherits" not in profile:
        return profile
    parent = str(Path(relative).parent / (profile["inherits"] + ".json"))
    return {**resolve(parent), **profile}


class RinkhalsProfileTests(unittest.TestCase):
    def test_beta_is_registered_and_uses_plain_moonraker_gcode(self):
        index = load("Anycubic.json")
        for section, name, path in [
            ("machine_model_list", MODEL, f"machine/{MODEL}.json"),
            ("machine_list", MACHINE, f"machine/{MACHINE}.json"),
        ]:
            self.assertIn({"name": name, "sub_path": path}, index[section])
        machine = resolve(f"Anycubic/machine/{MACHINE}.json")
        self.assertEqual(machine["host_type"], "moonraker")
        self.assertEqual(machine["use_3mf"], "0")
        self.assertEqual(machine["printer_model"], MODEL)
        self.assertEqual(machine["single_extruder_multi_material"], "0")
        self.assertEqual(machine["change_filament_gcode"], "")
        self.assertEqual(machine["machine_pause_gcode"], "PAUSE")
        self.assertNotIn("G9111", machine["machine_start_gcode"])
        self.assertNotIn("Y270", machine["machine_end_gcode"])
        self.assertIn("G90", machine["machine_start_gcode"])
        self.assertIn("M83", machine["machine_start_gcode"])
        self.assertIn("BED_MESH_PROFILE LOAD=default", machine["machine_start_gcode"])
        for key, section in [("default_print_profile", "process_list"),
                             ("default_filament_profile", "filament_list")]:
            names = machine[key] if isinstance(machine[key], list) else [machine[key]]
            for name in names:
                entry = next(e for e in index[section] if e["name"] == name)
                profile = resolve("Anycubic/" + entry["sub_path"])
                self.assertIn(MACHINE, profile["compatible_printers"])

    def test_stock_s1_keeps_its_firmware_contract(self):
        stock = resolve("Anycubic/machine/Anycubic Kobra S1 0.4 nozzle.json")
        self.assertEqual(stock["host_type"], "octoprint")
        self.assertIn("G9111", stock["machine_start_gcode"])
        self.assertEqual(stock["machine_pause_gcode"], "M601")


if __name__ == "__main__":
    unittest.main()
