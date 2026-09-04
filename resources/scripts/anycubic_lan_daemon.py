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
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
import paho.mqtt.client as mqtt

PRINTER_IP = "192.168.1.133"
PRINTER_HTTP_PORT = 18910
PRINTER_MQTT_PORT = 9883
HTTP_PORT = 18988

telemetry = {
    "connected": False,
    "ip": PRINTER_IP,
    "model": "Anycubic Kobra X",
    "state": "idle",
    "progress": 0,
    "job_name": "Ready",
    "curr_layer": 0,
    "total_layers": 0,
    "print_time": 0,
    "remain_time": 0,
    "nozzle_temp": 0,
    "target_nozzle_temp": 0,
    "bed_temp": 0,
    "target_bed_temp": 0,
    "camera_url": f"http://{PRINTER_IP}:18088/",
    "upload_url": f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/gcode_upload",
    "light": 1,
    "speed_mode": 2,
    "filaments": [
        {"slot": 1, "color": "#009639", "type": "PLA", "temp": 210, "loaded": False},
        {"slot": 2, "color": "#75787b", "type": "PLA", "temp": 210, "loaded": False},
        {"slot": 3, "color": "#fddb27", "type": "PLA+", "temp": 235, "loaded": True},
        {"slot": 4, "color": "#ff8da1", "type": "PLA", "temp": 210, "loaded": False}
    ]
}

mqtt_client = None
creds = {}
model_id = "20030"
device_id = "ce8784fb8feaf6ed93ecc8047e7d7665"
pending_cleanup = []

def rgb_to_hex(r, g, b):
    return f"#{r:02x}{g:02x}{b:02x}"

def fetch_credentials():
    global creds, model_id, device_id
    try:
        req = urllib.request.Request(f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/info")
        with urllib.request.urlopen(req, timeout=3) as r:
            info = json.loads(r.read().decode())
        token = info.get("token", "")
        if not token:
            return False

        ts = int(time.time() * 1000)
        nonce = "".join(random.choices(string.ascii_letters + string.digits, k=6))
        k1 = token[:16]
        k2 = token[16:32]
        sign = hashlib.md5((hashlib.md5(k1.encode()).hexdigest() + str(ts) + nonce).encode()).hexdigest()
        url = f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/ctrl?ts={ts}&nonce={nonce}&sign={sign}&did=orca-bridge"

        req = urllib.request.Request(url, data=b"", headers={"Content-Length": "0", "User-Agent": "AnycubicSlicerNext/2.0.0.2"})
        with urllib.request.urlopen(req, timeout=3) as r:
            res = json.loads(r.read().decode())

        iv = res["data"]["token"].encode()
        raw = base64.b64decode(res["data"]["info"])
        cipher = Cipher(algorithms.AES(k2.encode()), modes.CBC(iv))
        decryptor = cipher.decryptor()
        plain = decryptor.update(raw) + decryptor.finalize()
        pad = plain[-1]
        creds = json.loads(plain[:-pad].decode())

        model_id = str(creds.get("modelId", "20030"))
        device_id = creds.get("deviceId", device_id)
        return True
    except Exception as e:
        print(f"[Bridge] Handshake error: {e}")
        return False

def on_mqtt_connect(c, userdata, flags, rc, properties=None):
    global telemetry
    print(f"[Bridge] MQTT Connected to Kobra X, rc={rc}")
    telemetry["connected"] = True
    c.subscribe(f"anycubic/anycubicCloud/v1/printer/public/{model_id}/{device_id}/#")
    query_all()

def on_mqtt_message(c, userdata, msg):
    global telemetry, pending_cleanup
    try:
        payload = json.loads(msg.payload.decode("utf-8", errors="ignore"))
        t = msg.topic.split("/")[-2]
        data = payload.get("data")
        if not data:
            return

        if t == "tempature" or t == "temp":
            telemetry["nozzle_temp"] = data.get("curr_nozzle_temp", telemetry["nozzle_temp"])
            telemetry["target_nozzle_temp"] = data.get("target_nozzle_temp", telemetry["target_nozzle_temp"])
            telemetry["bed_temp"] = data.get("curr_hotbed_temp", telemetry["bed_temp"])
            telemetry["target_bed_temp"] = data.get("target_hotbed_temp", telemetry["target_bed_temp"])

        elif t == "info":
            temp = data.get("temp", {})
            if temp:
                telemetry["nozzle_temp"] = temp.get("curr_nozzle_temp", telemetry["nozzle_temp"])
                telemetry["target_nozzle_temp"] = temp.get("target_nozzle_temp", telemetry["target_nozzle_temp"])
                telemetry["bed_temp"] = temp.get("curr_hotbed_temp", telemetry["bed_temp"])
                telemetry["target_bed_temp"] = temp.get("target_hotbed_temp", telemetry["target_bed_temp"])

            project = data.get("project", {})
            if project:
                telemetry["state"] = project.get("state", telemetry["state"])
                telemetry["progress"] = project.get("progress", telemetry["progress"])
                telemetry["job_name"] = project.get("filename", telemetry["job_name"])
                telemetry["curr_layer"] = project.get("curr_layer", telemetry["curr_layer"])
                telemetry["total_layers"] = project.get("total_layers", telemetry["total_layers"])
                telemetry["remain_time"] = project.get("remain_time", telemetry["remain_time"])
                telemetry["print_time"] = project.get("print_time", telemetry["print_time"])
                telemetry["speed_mode"] = project.get("print_speed_mode", telemetry["speed_mode"])

            urls = data.get("urls", {})
            if "rtspUrl" in urls and urls["rtspUrl"]:
                telemetry["camera_url"] = urls["rtspUrl"]
            if "fileUploadurl" in urls and urls["fileUploadurl"]:
                telemetry["upload_url"] = urls["fileUploadurl"]

        elif t == "status":
            st = payload.get("state", "")
            if st:
                telemetry["state"] = st

        elif t == "light":
            lights = data.get("lights", [])
            if lights and len(lights) > 0:
                telemetry["light"] = lights[0].get("status", telemetry["light"])

        elif t == "multiColorBox":
            box_list = data.get("multi_color_box", [])
            if box_list and len(box_list) > 0:
                box = box_list[0]
                loaded_slot = box.get("loaded_slot", -1)
                slots = box.get("slots", [])
                new_filaments = []
                for s in slots:
                    idx = s.get("index", 0)
                    col = s.get("color", [0, 210, 255])
                    hex_col = rgb_to_hex(col[0], col[1], col[2])
                    m_type = s.get("type", "PLA")
                    new_filaments.append({
                        "slot": idx + 1,
                        "color": hex_col,
                        "type": m_type,
                        "temp": 235 if "PETG" in m_type or "+" in m_type else 210,
                        "loaded": (idx == loaded_slot)
                    })
                if new_filaments:
                    telemetry["filaments"] = new_filaments

    except Exception as e:
        pass

def query_all():
    global mqtt_client
    if not mqtt_client:
        return
    for q_type in ["tempature", "status", "multiColorBox", "info", "light"]:
        topic = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/{q_type}"
        msg = {
            "type": q_type,
            "action": "query" if q_type not in ["multiColorBox"] else "getInfo",
            "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
            "timestamp": int(time.time() * 1000),
            "data": None
        }
        try:
            mqtt_client.publish(topic, json.dumps(msg))
        except:
            pass

def mqtt_worker():
    global mqtt_client
    while True:
        if not fetch_credentials():
            time.sleep(3)
            continue

        try:
            nonce = "".join(random.choices(string.ascii_letters + string.digits, k=6))
            c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="orca-daemon-" + nonce)
            c.username_pw_set(creds.get("username"), creds.get("password"))
            c.tls_set(cert_reqs=ssl.CERT_NONE)
            c.tls_insecure_set(True)
            c.on_connect = on_mqtt_connect
            c.on_message = on_mqtt_message
            mqtt_client = c

            c.connect(PRINTER_IP, PRINTER_MQTT_PORT, 60)
            c.loop_start()

            while True:
                time.sleep(2)
                query_all()

        except Exception as e:
            print(f"[Bridge] MQTT exception: {e}")
            time.sleep(3)

def upload_and_run_gcode(gcode_text, delete_after=True):
    ts = int(time.time())
    rand_id = "".join(random.choices(string.ascii_lowercase + string.digits, k=4))
    fname = f"_orcacubic_command_{ts}_{rand_id}.gcode"
    content = f"; generated by OrcaCubic Run G-code file\n; policy: run-gcode-v1\n; temporary: true\n{gcode_text.strip()}\nM400\n"
    
    upload_url = telemetry.get("upload_url", f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/gcode_upload")
    boundary = "----WebKitFormBoundary" + "".join(random.choices(string.ascii_letters + string.digits, k=16))
    
    part1 = f'--{boundary}\r\nContent-Disposition: form-data; name="filename"\r\n\r\n{fname}\r\n'
    part2 = f'--{boundary}\r\nContent-Disposition: form-data; name="gcode"; filename="{fname}"\r\nContent-Type: application/octet-stream\r\n\r\n'
    part3 = f'\r\n--{boundary}--\r\n'
    
    body = part1.encode('utf-8') + part2.encode('utf-8') + content.encode('utf-8') + part3.encode('utf-8')
    
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body)),
        "X-File-Length": str(len(content.encode('utf-8'))),
        "User-Agent": "AnycubicSlicerNext/2.0.0.2"
    }
    
    req = urllib.request.Request(upload_url, data=body, headers=headers)
    with urllib.request.urlopen(req, timeout=10) as r:
        resp = r.read().decode('utf-8', errors='ignore')
    print(f"[Bridge] Uploaded {fname}: {resp}")
    
    time.sleep(0.5)
    
    topic = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/print"
    msg = {
        "type": "print",
        "action": "start",
        "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
        "timestamp": int(time.time() * 1000),
        "data": {
            "file_name": fname,
            "use_ams": False
        }
    }
    if mqtt_client:
        mqtt_client.publish(topic, json.dumps(msg))
        print(f"[Bridge] Triggered run for {fname}")
        
    return {"status": "ok", "filename": fname}

class BridgeServer(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_GET(self):
        if self.path.startswith("/status") or self.path == "/" or self.path.startswith("/api/v1"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(telemetry).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"
        try:
            data = json.loads(body)
        except:
            data = {}

        result = {"status": "ok"}

        if self.path.startswith("/control"):
            action = data.get("action", "")
            if action in ["pause", "resume", "stop"]:
                topic = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/print"
                msg = {
                    "type": "print",
                    "action": action,
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": None
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))

            elif action == "light":
                status = int(data.get("status", 1))
                topic = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/light"
                msg = {
                    "type": "light",
                    "action": "set",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "brightness": 100,
                        "status": status,
                        "type": 3
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                telemetry["light"] = status

            elif action == "speed":
                mode = int(data.get("mode", 2))
                topic = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/print"
                msg = {
                    "type": "print",
                    "action": "setSpeedMode",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "print_speed_mode": mode
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                telemetry["speed_mode"] = mode

        elif self.path.startswith("/run_gcode"):
            gcode = data.get("gcode", "")
            delete_after = data.get("delete_after", True)
            if gcode.strip():
                try:
                    result = upload_and_run_gcode(gcode, delete_after=delete_after)
                except Exception as e:
                    result = {"status": "error", "message": str(e)}

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(result).encode("utf-8"))

    def log_message(self, format, *args):
        pass

def run_server():
    server = HTTPServer(("127.0.0.1", HTTP_PORT), BridgeServer)
    print(f"[Bridge] Local API Server running on http://127.0.0.1:{HTTP_PORT}")
    server.serve_forever()

if __name__ == "__main__":
    t1 = threading.Thread(target=mqtt_worker, daemon=True)
    t1.start()
    run_server()
