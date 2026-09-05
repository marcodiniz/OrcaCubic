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
    # Trigger video startCapture
    try:
        v_topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/video"
        v_msg = {
            "type": "video",
            "action": "startCapture",
            "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
            "timestamp": int(time.time() * 1000),
            "data": None
        }
        c.publish(v_topic, json.dumps(v_msg))
    except:
        pass

def on_mqtt_message(c, userdata, msg):
    global telemetry, pending_cleanup
    try:
        payload = json.loads(msg.payload.decode("utf-8", errors="ignore"))
        t = msg.topic.split("/")[-2]

        code = payload.get("code", 200)
        msg_str = payload.get("msg", "")
        state_str = payload.get("state", "")

        # Always capture printer LCD warnings and error reports
        if (code != 200 and code != 0) or state_str in ["failed", "abnormal"]:
            if msg_str:
                display_msg = msg_str
                if code == 10901 or "home" in msg_str.lower():
                    display_msg = "Home the axis before moving (Click 🏠 Home to calibrate zero position)."
                elif code == 10111:
                    display_msg = f"LCD Notice (10111): {msg_str}. Please click 🏠 Home before starting motion or print jobs."
                telemetry["last_error"] = display_msg
                telemetry["last_error_code"] = code
                telemetry["last_error_time"] = int(time.time())
                print(f"[Bridge] PRINTER LCD NOTICE ({code}): {display_msg}")

        data = payload.get("data")

        if t == "tempature" or t == "temp":
            if data:
                telemetry["nozzle_temp"] = data.get("curr_nozzle_temp", telemetry["nozzle_temp"])
                telemetry["target_nozzle_temp"] = data.get("target_nozzle_temp", telemetry["target_nozzle_temp"])
                telemetry["bed_temp"] = data.get("curr_hotbed_temp", telemetry["bed_temp"])
                telemetry["target_bed_temp"] = data.get("target_hotbed_temp", telemetry["target_bed_temp"])

        elif t == "info":
            if data:
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

        elif t == "axis":
            if code != 200 and msg_str:
                telemetry["last_error"] = msg_str
                telemetry["last_error_code"] = code
                telemetry["last_error_time"] = int(time.time())
            elif code == 200 and telemetry.get("last_error") == "Home the axis before moving":
                telemetry["last_error"] = ""

        elif t == "light":
            if data:
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
    
    # Construct complete, compliant G-code with headers and statistics
    content = (
        "; HEADER_BLOCK_START\n"
        "; generated by AnycubicSlicerNext 2.0.0.2\n"
        "; total layer number: 1\n"
        "; filament_density: 1.24\n"
        "; filament_diameter: 1.75\n"
        "; max_z_height: 10.0\n"
        "; HEADER_BLOCK_END\n\n"
        f"{gcode_text.strip()}\n"
        "M400\n\n"
        "; statistics = begin\n"
        "; used_filament = 0.00\n"
        "; print_time = 0m 10s\n"
        "; model_size = 10.00,10.00,10.00\n"
        "; total_layers = 1\n"
        "; statistics = end\n"
    )
    content_bytes = content.encode("utf-8")
    fmd5 = hashlib.md5(content_bytes).hexdigest().lower()
    fsize = len(content_bytes)

    upload_url = telemetry.get("upload_url", f"http://{PRINTER_IP}:{PRINTER_HTTP_PORT}/gcode_upload")
    boundary = "----WebKitFormBoundary" + "".join(random.choices(string.ascii_letters + string.digits, k=16))
    
    part1 = f'--{boundary}\r\nContent-Disposition: form-data; name="filename"\r\n\r\n{fname}\r\n'
    part2 = f'--{boundary}\r\nContent-Disposition: form-data; name="gcode"; filename="{fname}"\r\nContent-Type: application/octet-stream\r\n\r\n'
    part3 = f'\r\n--{boundary}--\r\n'
    
    body = part1.encode('utf-8') + part2.encode('utf-8') + content_bytes + part3.encode('utf-8')
    
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body)),
        "X-File-Length": str(fsize),
        "X-BBL-Client-Name": "AnycubicSlicerNext",
        "X-BBL-Client-Type": "slicer",
        "X-BBL-Client-Version": "01.03.09.04",
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
            "taskid": "-1",
            "filename": fname,
            "url": "",
            "md5": fmd5,
            "filepath": None,
            "filetype": 1,
            "project_type": 1,
            "filesize": fsize,
            "ams_settings": {
                "use_ams": True,
                "ams_box_mapping": [
                    {
                        "ams_color": [253, 219, 39],
                        "ams_index": 2,
                        "material_type": "PLA+",
                        "paint_color": [253, 219, 39],
                        "paint_index": 0
                    }
                ]
            },
            "task_settings": {
                "auto_leveling": 0,
                "vibration_compensation": 0,
                "flow_calibration": 0,
                "dry_mode": 0,
                "ai_settings": {"status": 0, "count": 0, "type": 0},
                "timelapse": {"status": 0, "count": 0, "type": 0},
                "drying_settings": {"status": 0, "target_temp": 0, "duration": 0, "remain_time": 0},
                "model_objects_skip_parts": []
            }
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
            # Auto-expire historical alerts after 20 seconds or when printing
            if telemetry.get("last_error"):
                err_age = time.time() - telemetry.get("last_error_time", 0)
                st = telemetry.get("state", "").lower()
                if err_age > 20 or st in ["printing", "auto_leveling", "heating", "busy"]:
                    telemetry["last_error"] = ""
                    telemetry["last_error_code"] = 0

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
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/light"
                msg = {
                    "type": "light",
                    "action": "control",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "type": 3,
                        "status": status,
                        "brightness": 100 if status == 1 else 0
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print(f"[Bridge] Published light {status} to {topic}")
                telemetry["light"] = status

            elif action == "start_capture" or action == "video_start":
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/video"
                msg = {
                    "type": "video",
                    "action": "startCapture",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": None
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print(f"[Bridge] Published startCapture to {topic}")

            elif action == "stop_capture" or action == "video_stop":
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/video"
                msg = {
                    "type": "video",
                    "action": "stopCapture",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": None
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print(f"[Bridge] Published stopCapture to {topic}")

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

            elif action == "jog":
                axis_name = data.get("axis", "X").upper()
                axis_map = {"X": 1, "Y": 2, "Z": 3, "XY": 4, "XYZ": 5}
                ax = axis_map.get(axis_name, 1)
                direction = int(data.get("dir", 1))
                move_type = 1 if direction > 0 else 0
                dist = abs(float(data.get("dist", 10)))
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/axis"
                msg = {
                    "type": "axis",
                    "action": "move",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "axis": ax,
                        "move_type": move_type,
                        "distance": dist
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print(f"[Bridge] Published jog {axis_name} {direction} {dist}mm")

            elif action == "home":
                axis_name = data.get("axis", "XYZ").upper()
                ax = 5 if axis_name == "XYZ" else (4 if axis_name == "XY" else (3 if axis_name == "Z" else 1))
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/axis"
                msg = {
                    "type": "axis",
                    "action": "move",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "axis": ax,
                        "move_type": 2,
                        "distance": 0
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print(f"[Bridge] Published Home {axis_name}")

            elif action == "clear_alert":
                telemetry["last_error"] = ""
                telemetry["last_error_code"] = 0
                telemetry["axis_error"] = ""

            elif action == "motor_off" or action == "turnOff":
                topic = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/axis"
                msg = {
                    "type": "axis",
                    "action": "turnOff",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": None
                }
                if mqtt_client:
                    mqtt_client.publish(topic, json.dumps(msg))
                    print("[Bridge] Published Motor Turn Off")

            elif action == "start_print_job":
                filename = data.get("filename", "")
                if filename and mqtt_client:
                    topic_print = f"anycubic/anycubicCloud/v1/slicer/printer/{model_id}/{device_id}/print"
                    # Construct 1-to-1 AMS box mapping for all 4 slots matching physical feeder
                    ams_mapping = data.get("ams_box_mapping")
                    if not ams_mapping:
                        ams_mapping = []
                        fils = telemetry.get("filaments", [])
                        for i in range(min(4, len(fils) if fils else 4)):
                            f = fils[i] if fils and i < len(fils) else {}
                            col = f.get("color", "#23a3c7")
                            if isinstance(col, str) and col.startswith("#"):
                                c_hex = col.lstrip("#")
                                r = int(c_hex[0:2], 16) if len(c_hex) >= 2 else 0
                                g = int(c_hex[2:4], 16) if len(c_hex) >= 4 else 210
                                b = int(c_hex[4:6], 16) if len(c_hex) >= 6 else 255
                                rgb = [r, g, b]
                            else:
                                rgb = [35, 163, 199]
                            ams_mapping.append({
                                "ams_index": i,
                                "paint_index": i,
                                "material_type": f.get("type", "PLA"),
                                "ams_color": rgb,
                                "paint_color": rgb
                            })

                    msg = {
                        "type": "print",
                        "action": "start",
                        "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                        "timestamp": int(time.time() * 1000),
                        "data": {
                            "taskid": "-1",
                            "filename": filename,
                            "url": "",
                            "md5": "",
                            "filepath": None,
                            "filetype": 1,
                            "project_type": 1,
                            "filesize": 0,
                            "ams_settings": {
                                "use_ams": True,
                                "ams_box_mapping": ams_mapping
                            },
                            "task_settings": {
                                "auto_leveling": 1,
                                "vibration_compensation": 0,
                                "flow_calibration": 0,
                                "dry_mode": 0,
                                "ai_settings": {"status": 0, "count": 0, "type": 0},
                                "timelapse": {"status": 0, "count": 0, "type": 0},
                                "drying_settings": {"status": 0, "target_temp": 0, "duration": 0, "remain_time": 0},
                                "model_objects_skip_parts": []
                            }
                        }
                    }
                    mqtt_client.publish(topic_print, json.dumps(msg))
                    print(f"[Bridge] Published print:start for {filename} with {len(ams_mapping)} mapped slots to {topic_print}")

            elif action == "feed_filament":
                slot_idx = int(data.get("slot", 0))
                m_type = data.get("type", "PLA")
                topic_feed = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/multiColorBox"
                msg = {
                    "type": "multiColorBox",
                    "action": "feedFilament",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "multi_color_box": [{
                            "id": -1,
                            "feed_status": {
                                "slot_index": slot_idx,
                                "type": m_type
                            }
                        }]
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic_feed, json.dumps(msg))
                    print(f"[Bridge] Published feedFilament slot {slot_idx} ({m_type})")

            elif action == "temp":
                heater = data.get("heater", "nozzle")
                val = int(data.get("value", 0))
                topic_temp = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/tempature"
                if heater == "nozzle":
                    t_type = 0
                    bed_val = 0
                    nozzle_val = val
                    telemetry["target_nozzle_temp"] = val
                else:
                    t_type = 1
                    bed_val = val
                    nozzle_val = 0
                    telemetry["target_bed_temp"] = val
                msg = {
                    "type": "tempature",
                    "action": "set",
                    "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "type": t_type,
                        "target_hotbed_temp": bed_val,
                        "target_nozzle_temp": nozzle_val
                    }
                }
                if mqtt_client:
                    mqtt_client.publish(topic_temp, json.dumps(msg))
                    print(f"[Bridge] Published set tempature {heater}={val} to {topic_temp}")

            elif action == "extrude":
                direction = data.get("dir", "extrude")
                dist = abs(float(data.get("dist", 10)))
                sign = 1 if direction == "extrude" else -1
                e_dist = sign * dist
                gcode = f"; Extrude filament\nM83\nG1 E{e_dist:.1f} F150\nM400\n"
                try:
                    result = upload_and_run_gcode(gcode, delete_after=True)
                    print(f"[Bridge] Extrude executed: {direction} {dist}mm")
                except Exception as e:
                    result = {"status": "error", "message": str(e)}

        elif self.path.endswith("/mach_mqtt/publish"):
            topic = data.get("topic", "")
            payload_raw = data.get("payload", "")
            if topic and mqtt_client:
                mqtt_client.publish(topic, payload_raw)
                print(f"[Bridge] Proxy-published to {topic}")
            result = {"code": 200, "data": None, "msg": "ok"}

        elif self.path.startswith("/run_gcode"):
            gcode = data.get("gcode", "")
            delete_after = data.get("delete_after", True)
            if gcode.strip():
                try:
                    result = upload_and_run_gcode(gcode, delete_after=delete_after)
                except Exception as e:
                    result = {"status": "error", "message": str(e)}

        elif self.path.startswith("/sync_to_printer"):
            slots_data = data.get("slots", [])
            if slots_data and mqtt_client:
                # Kobra X firmware setInfo requires ONE slot per message in slots array
                topic_web = f"anycubic/anycubicCloud/v1/web/printer/{model_id}/{device_id}/multiColorBox"
                for s in slots_data:
                    idx = int(s.get("index", 0))
                    m_type = s.get("type", "PLA")
                    col = s.get("color")
                    if isinstance(col, str) and col.startswith("#"):
                        c_hex = col.lstrip("#")
                        r = int(c_hex[0:2], 16) if len(c_hex) >= 2 else 0
                        g = int(c_hex[2:4], 16) if len(c_hex) >= 4 else 210
                        b = int(c_hex[4:6], 16) if len(c_hex) >= 6 else 255
                        rgb = [r, g, b]
                    elif isinstance(col, list) and len(col) >= 3:
                        rgb = [col[0], col[1], col[2]]
                    else:
                        rgb = [35, 163, 199]

                    slot_obj = {
                        "index": idx,
                        "type": m_type,
                        "color": rgb,
                        "color_group": [rgb + [255]]
                    }
                    msg = {
                        "type": "multiColorBox",
                        "action": "setInfo",
                        "msgid": "".join(random.choices(string.hexdigits.lower(), k=32)),
                        "timestamp": int(time.time() * 1000),
                        "data": {
                            "multi_color_box": [
                                {
                                    "id": -1,
                                    "slots": [slot_obj]
                                }
                            ]
                        }
                    }
                    mqtt_client.publish(topic_web, json.dumps(msg))
                    print(f"[Bridge] Published setInfo for slot {idx} ({m_type}) to printer")
                    time.sleep(0.15)

                time.sleep(0.4)
                query_all()
            result = {"status": "ok"}

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
