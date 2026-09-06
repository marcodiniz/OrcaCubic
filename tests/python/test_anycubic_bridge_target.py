import http.client
import importlib.util
import json
import os
import sys
import threading
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "resources" / "scripts" / "anycubic_lan_daemon.py"
os.environ["ORCACUBIC_PRINTER_IP"] = "192.0.2.20"
os.environ["ORCACUBIC_BRIDGE_TOKEN"] = "test-token"
spec = importlib.util.spec_from_file_location("anycubic_lan_daemon", MODULE_PATH)
daemon = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(daemon)


class TargetBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        daemon.PRINTER_IP = "192.0.2.20"
        daemon.BRIDGE_TOKEN = "test-token"
        cls.server = daemon.HTTPServer(("127.0.0.1", 0), daemon.BridgeServer)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def post_control(self, expected_printer):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        body = json.dumps({"action": "clear_alert"})
        connection.request(
            "POST",
            "/control",
            body,
            {
                "Content-Type": "application/json",
                "Content-Length": str(len(body)),
                "X-OrcaCubic-Token": "test-token",
                "X-OrcaCubic-Printer": expected_printer,
            },
        )
        response = connection.getresponse()
        payload = json.loads(response.read())
        connection.close()
        return response.status, payload

    def test_matching_device_target_is_accepted(self):
        status, payload = self.post_control("192.0.2.20")
        self.assertEqual(status, 200)
        self.assertEqual(payload["status"], "ok")

    def test_stale_device_target_is_rejected_before_control(self):
        status, payload = self.post_control("192.0.2.21")
        self.assertEqual(status, 409)
        self.assertEqual(payload["message"], "Device selection changed")


class MaterialSystemTests(unittest.TestCase):
    def setUp(self):
        daemon.telemetry["has_multi_color_box"] = False
        daemon.telemetry["material_boxes"] = []
        daemon.telemetry["external_spool"] = None
        daemon.telemetry["filaments"] = []

    def test_external_ace_report_with_optional_fields_is_detected(self):
        daemon.apply_multi_color_box_report({
            "multi_color_box": [{
                "id": 0,
                "slots": [{"index": 0, "type": "PLA", "color": [1, 2, 3], "edit_status": 1}],
            }]
        })
        self.assertTrue(daemon.telemetry["has_multi_color_box"])
        self.assertEqual(daemon.telemetry["material_boxes"][0]["id"], 0)
        self.assertEqual(daemon.telemetry["filaments"][0]["slot"], 0)

    def test_multiple_ace_units_keep_box_and_global_slot_identity(self):
        daemon.apply_multi_color_box_report({
            "max_box_num": 4,
            "multi_color_box": [
                {"id": 0, "slots": [{"index": 3, "type": "PLA", "color": [10, 20, 30], "status": 5}]},
                {"id": 1, "slots": [{"index": 2, "type": "PETG", "color": [40, 50, 60], "edit_status": 1}]},
            ],
        })
        slots = daemon.telemetry["filaments"]
        self.assertEqual([(s["box_id"], s["box_slot"], s["slot"]) for s in slots], [(0, 3, 3), (1, 2, 6)])
        self.assertEqual(len(daemon.telemetry["material_boxes"]), 2)

    def test_builtin_negative_id_rack_keeps_unique_zero_based_slots(self):
        daemon.apply_multi_color_box_report({"multi_color_box": [{"id": -1, "slots": [
            {"index": 0, "type": "PLA", "edit_status": 1},
            {"index": 3, "type": "PETG", "edit_status": 1},
        ]}]})
        box = daemon.telemetry["material_boxes"][0]
        self.assertEqual(box["source"], "rack")
        self.assertEqual([slot["slot"] for slot in box["slots"]], [0, 3])

    def test_mixed_negative_id_entry_uses_multicolorbox_not_extfilbox(self):
        daemon.apply_multi_color_box_report({"head_tools_model": 1, "multi_color_box": [
            {"id": -1, "slots": [{"index": 0, "type": "TPU", "edit_status": 1}]},
            {"id": 0, "slots": [{"index": 0, "type": "PLA", "edit_status": 1}]},
        ]})
        rack = next(box for box in daemon.telemetry["material_boxes"] if box["id"] == -1)
        self.assertEqual(rack["source"], "external_mcb")
        suffix, payload = daemon.build_material_update_messages([rack["slots"][0]])[0]
        self.assertEqual(suffix, "multiColorBox")
        self.assertEqual(payload["data"]["multi_color_box"][0]["id"], -1)

    def test_status_four_slot_is_not_available(self):
        daemon.apply_multi_color_box_report({"multi_color_box": [{"id": 0, "slots": [
            {"index": 0, "type": "PLA", "status": 4},
        ]}]})
        self.assertFalse(daemon.telemetry["filaments"][0]["available"])

    def test_external_spool_is_used_when_no_ace_is_connected(self):
        daemon.apply_external_spool_report({"type": "TPU", "color": [7, 8, 9], "loaded": 1})
        slot = daemon.telemetry["filaments"][0]
        self.assertEqual(slot["source"], "external")
        self.assertEqual(slot["slot"], -1)
        self.assertEqual(slot["type"], "TPU")

    def test_external_spool_and_ace_units_are_retained_together(self):
        daemon.apply_external_spool_report({"type": "TPU", "color": [7, 8, 9], "loaded": 1})
        daemon.apply_multi_color_box_report({"multi_color_box": [
            {"id": 0, "slots": [{"index": 0, "type": "PLA", "edit_status": 1}]},
            {"id": 1, "slots": [{"index": 0, "type": "PETG", "edit_status": 1}]},
        ]})
        self.assertEqual([box["source"] for box in daemon.telemetry["material_boxes"]], ["external", "ace", "ace"])

    def test_live_empty_ace_report_clears_stale_box_data(self):
        daemon.apply_multi_color_box_report({"multi_color_box": [{"id": 0, "slots": [{"index": 0, "type": "PLA", "edit_status": 1}]}]})
        daemon.apply_multi_color_box_report({"multi_color_box": []})
        self.assertFalse(daemon.telemetry["has_multi_color_box"])
        self.assertEqual(daemon.telemetry["material_boxes"], [])
        self.assertEqual(daemon.telemetry["filaments"], [])

    def test_live_empty_ace_report_keeps_external_spool(self):
        daemon.apply_external_spool_report({"type": "TPU", "color": [7, 8, 9], "loaded": 1})
        daemon.apply_multi_color_box_report({"multi_color_box": []})
        self.assertFalse(daemon.telemetry["has_multi_color_box"])
        self.assertEqual([box["source"] for box in daemon.telemetry["material_boxes"]], ["external"])

    def test_material_updates_keep_reported_ace_box_id(self):
        messages = daemon.build_material_update_messages([
            {"source": "ace", "box_id": 2, "index": 3, "type": "PETG", "color": "#010203"}
        ])
        suffix, payload = messages[0]
        self.assertEqual(suffix, "multiColorBox")
        self.assertEqual(payload["data"]["multi_color_box"][0]["id"], 2)
        self.assertEqual(payload["data"]["multi_color_box"][0]["slots"][0]["index"], 3)

    def test_external_spool_updates_use_extfilbox_protocol(self):
        messages = daemon.build_material_update_messages([
            {"source": "external", "box_id": -1, "index": 0, "type": "TPU", "color": "#070809"}
        ])
        suffix, payload = messages[0]
        self.assertEqual(suffix, "extfilbox")
        self.assertEqual(payload["action"], "setInfo")
        self.assertEqual(payload["data"], {"type": "TPU", "color": [7, 8, 9]})

    def test_partial_external_ack_preserves_known_material(self):
        daemon.apply_external_spool_report({"type": "TPU", "color": [7, 8, 9], "loaded": 1, "edit_status": 1})
        daemon.apply_external_spool_report({"loaded": 1})
        slot = daemon.telemetry["external_spool"]
        self.assertEqual(slot["type"], "TPU")
        self.assertEqual(slot["color"], "#070809")
        self.assertTrue(slot["available"])

    def test_material_queries_request_peripheral_ace_and_external_spool_state(self):
        class FakeClient:
            def __init__(self):
                self.messages = []

            def publish(self, topic, body):
                self.messages.append((topic.rsplit("/", 1)[-1], json.loads(body)["action"]))

        previous = daemon.mqtt_client
        daemon.mqtt_client = FakeClient()
        try:
            daemon.query_all()
            self.assertIn(("peripherie", "query"), daemon.mqtt_client.messages)
            self.assertIn(("multiColorBox", "getInfo"), daemon.mqtt_client.messages)
            self.assertIn(("extfilbox", "getInfo"), daemon.mqtt_client.messages)
        finally:
            daemon.mqtt_client = previous

    def test_partial_setinfo_ack_does_not_replace_complete_ace_snapshot(self):
        daemon.apply_multi_color_box_report({
            "multi_color_box": [
                {"id": 0, "slots": [{"index": 0, "type": "PLA", "edit_status": 1}]},
                {"id": 1, "slots": [{"index": 0, "type": "PETG", "edit_status": 1}]},
            ]
        })
        class Message:
            topic = "anycubic/anycubicCloud/v1/printer/public/20030/device/multiColorBox/report"
            payload = json.dumps({
                "type": "multiColorBox", "action": "setInfo", "code": 200,
                "data": {"multi_color_box": [{"id": 0, "slots": [{"index": 0, "type": "ABS"}]}]},
            }).encode()

        daemon.on_mqtt_message(None, None, Message())
        boxes = daemon.telemetry["material_boxes"]
        self.assertEqual(len(boxes), 2)
        self.assertEqual(boxes[0]["slots"][0]["type"], "ABS")
        self.assertEqual(boxes[1]["slots"][0]["type"], "PETG")


if __name__ == "__main__":
    unittest.main()
