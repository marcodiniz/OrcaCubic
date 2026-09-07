#!/usr/bin/env python3
"""
Kobra X Interactive Command & Channel Test Server
Maintains a live MQTT TLS link to 192.168.1.133 and provides an interactive web UI.
"""
import sys
import os
import time
import json
import base64
import random
import string
import hashlib
import urllib.request
import ssl
from http.server import HTTPServer, BaseHTTPRequestHandler
import threading

# Add vendor directory for pyaes and paho-mqtt
VENDOR_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vendor")
if VENDOR_DIR not in sys.path:
    sys.path.insert(0, VENDOR_DIR)

import pyaes
import paho.mqtt.client as mqtt

PRINTER_IP = "192.168.1.133"
PRINTER_HTTP_PORT = 18910
PRINTER_MQTT_PORT = 9883
HTTP_PORT = 18990

telemetry = {
    "connected": False,
    "ip": PRINTER_IP,
    "model": "Anycubic Kobra X",
    "firmware": "",
    "temp": {"curr_nozzle_temp": 0, "curr_hotbed_temp": 0, "target_nozzle_temp": 0, "target_hotbed_temp": 0},
    "extrude_control": {"index": -1, "current_status": 0, "has_filaments": 0, "move_type": 0},
    "slots": [],
    "logs": [],
    "last_report": {}
}

model_id = "20030"
device_id = ""
mqtt_client = None

def add_log(msg_type, topic, data):
    entry = {
        "time": time.strftime("%H:%M:%S"),
        "type": msg_type,
        "topic": topic,
        "data": data
    }
    telemetry["logs"].append(entry)
    if len(telemetry["logs"]) > 50:
        telemetry["logs"].pop(0)

def fetch_credentials():
    global model_id, device_id
    try:
        req = urllib.request.Request(f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/info")
        with urllib.request.urlopen(req, timeout=3) as r:
            info = json.loads(r.read().decode())
        token = info.get("token", "")
        if not token:
            return None

        telemetry["model"] = info.get("deviceName") or "Anycubic Kobra X"
        telemetry["firmware"] = info.get("firmwareVersion") or ""

        ts = int(time.time() * 1000)
        nonce = "".join(random.choices(string.ascii_letters + string.digits, k=6))
        k1 = token[:16]
        k2 = token[16:32]
        sign = hashlib.md5((hashlib.md5(k1.encode()).hexdigest() + str(ts) + nonce).encode()).hexdigest()
        url = f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/ctrl?ts={ts}&nonce={nonce}&sign={sign}&did=tester-console"

        req = urllib.request.Request(url, data=b"", headers={"Content-Length": "0", "User-Agent": "AnycubicSlicerNext/2.0.0.2"})
        with urllib.request.urlopen(req, timeout=3) as r:
            res = json.loads(r.read().decode())

        iv = res["data"]["token"].encode()
        raw = base64.b64decode(res["data"]["info"])
        decrypter = pyaes.Decrypter(pyaes.AESModeOfOperationCBC(k2.encode(), iv=iv))
        plain = decrypter.feed(raw) + decrypter.feed()
        creds = json.loads(plain.decode())

        model_id = str(creds.get("modelId") or info.get("modelId") or "20030")
        device_id = creds.get("deviceId", "")
        return creds
    except Exception as e:
        print(f"[Tester] Handshake error: {e}")
        return None

def on_mqtt_connect(c, userdata, flags, rc, properties=None):
    print(f"[Tester] MQTT Connected (rc={rc})")
    telemetry["connected"] = True
    c.subscribe(f"anycubic/anycubicCloud/v1/printer/public/{model_id}/{device_id}/#")
    query_all()

def on_mqtt_message(c, userdata, msg):
    try:
        topic_suffix = msg.topic.split("/")[-2]
        payload = json.loads(msg.payload.decode("utf-8", errors="ignore"))
        data = payload.get("data")
        add_log("RECV", topic_suffix, payload)

        if topic_suffix == "extrudeControl" and data:
            telemetry["extrude_control"]["index"] = data.get("index", -1)
            telemetry["extrude_control"]["current_status"] = data.get("current_status", 0)
            telemetry["extrude_control"]["has_filaments"] = data.get("has_filaments", 0)
            telemetry["extrude_control"]["move_type"] = data.get("move_type", 0)
        elif topic_suffix == "tempature" and data:
            telemetry["temp"]["curr_nozzle_temp"] = data.get("curr_nozzle_temp", 0)
            telemetry["temp"]["curr_hotbed_temp"] = data.get("curr_hotbed_temp", 0)
            telemetry["temp"]["target_nozzle_temp"] = data.get("target_nozzle_temp", 0)
            telemetry["temp"]["target_hotbed_temp"] = data.get("target_hotbed_temp", 0)
        elif topic_suffix == "multiColorBox" and data:
            boxes = data.get("multi_color_box", [])
            if boxes:
                telemetry["slots"] = boxes[0].get("slots", [])
                telemetry["loaded_slot"] = boxes[0].get("loaded_slot", -1)
    except Exception as e:
        print(f"[Tester] Message error: {e}")

def query_all():
    if not mqtt_client:
        return
    for item in [("extrudeControl", "getInfo"), ("multiColorBox", "getInfo"), ("tempature", "query"), ("status", "query")]:
        topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/{item[0]}"
        msg = {
            "type": item[0],
            "action": item[1],
            "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
            "timestamp": int(time.time() * 1000),
            "data": None
        }
        mqtt_client.publish(topic, json.dumps(msg))

def start_mqtt():
    global mqtt_client
    creds = fetch_credentials()
    if not creds:
        print("[Tester] Could not fetch credentials")
        return
    client = mqtt.Client(client_id=f"tester-{device_id[:8]}", protocol=mqtt.MQTTv311)
    client.username_pw_set(creds["username"], creds["password"])
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    client.tls_set_context(ctx)
    client.on_connect = on_mqtt_connect
    client.on_message = on_mqtt_message
    try:
        client.connect(PRINTER_IP, PRINTER_MQTT_PORT, 60)
        client.loop_start()
        mqtt_client = client
    except Exception as e:
        print(f"[Tester] MQTT Connect error: {e}")

class TestServerHandler(BaseHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path == "/status":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(telemetry).encode("utf-8"))
        elif self.path == "/query":
            query_all()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        else:
            html_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kobra_x_tester.html")
            if os.path.exists(html_path):
                with open(html_path, "rb") as f:
                    content = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(content)
            else:
                self.send_response(404)
                self.end_headers()

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"
        try:
            req_data = json.loads(body)
        except:
            req_data = {}

        resp = {"status": "ok"}
        if self.path == "/switch_channel":
            channel_index = int(req_data.get("index", 0))
            topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/extrudeControl"
            msg = {
                "type": "extrudeControl",
                "action": "switchChannel",
                "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                "timestamp": int(time.time() * 1000),
                "data": {"index": channel_index}
            }
            if mqtt_client:
                mqtt_client.publish(topic, json.dumps(msg))
                add_log("SEND", "extrudeControl (switchChannel)", msg)
                resp["sent"] = msg
            else:
                resp = {"status": "error", "message": "MQTT not connected"}

        elif self.path == "/feed":
            slot = int(req_data.get("slot", 0))
            feed_type = int(req_data.get("type", 1)) # 1 = feed, 2 = unfeed
            topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/multiColorBox"
            msg = {
                "type": "multiColorBox",
                "action": "feedFilament",
                "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                "timestamp": int(time.time() * 1000),
                "data": {
                    "multi_color_box": [{
                        "id": -1,
                        "feed_status": {
                            "slot_index": slot,
                            "type": "PLA" if feed_type == 1 else 2
                        }
                    }]
                }
            }
            if mqtt_client:
                mqtt_client.publish(topic, json.dumps(msg))
                add_log("SEND", f"multiColorBox (feedFilament type={feed_type})", msg)
                resp["sent"] = msg
            else:
                resp = {"status": "error", "message": "MQTT not connected"}

        elif self.path == "/send_raw_mqtt":
            sub_topic = req_data.get("sub_topic", "extrudeControl")
            action = req_data.get("action", "getInfo")
            payload_data = req_data.get("data", None)
            topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/{sub_topic}"
            msg = {
                "type": sub_topic,
                "action": action,
                "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                "timestamp": int(time.time() * 1000),
                "data": payload_data
            }
            if mqtt_client:
                mqtt_client.publish(topic, json.dumps(msg))
                add_log("SEND", sub_topic, msg)
                resp["sent"] = msg
            else:
                resp = {"status": "error", "message": "MQTT not connected"}

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(resp).encode("utf-8"))

def main():
    threading.Thread(target=start_mqtt, daemon=True).start()
    server = HTTPServer(("127.0.0.1", HTTP_PORT), TestServerHandler)
    print(f"[Tester] Local console ready on http://127.0.0.1:{HTTP_PORT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
