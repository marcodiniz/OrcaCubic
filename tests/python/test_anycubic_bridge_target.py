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


if __name__ == "__main__":
    unittest.main()
