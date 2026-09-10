from pathlib import Path
import re

p = Path(r"X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html")
s = p.read_text(encoding="utf-8")

# Preserve the complete authentic-style material modal and existing scripts, but replace the
# page dashboard grid with the stock Workbench V3 3-column / 2-row area order:
#   task | control | device
# camera | material | axis
start = s.index('    <!-- Main Grid -->')
end = s.index('    <script>', start)

new_grid = r'''    <!-- Workbench V3 Grid: stock Anycubic section order -->
    <main class="wb3-grid">
        <!-- Row 1 / Column 1: Task -->
        <section class="card job-card wb3-task">
            <div class="card-header">
                <h2>Active Print Job</h2>
                <span class="tag" id="print-state">Ready</span>
            </div>
            <div class="job-title-row">
                <div class="job-name" id="job-name">Ready</div>
                <div class="job-percent" id="job-pct">0%</div>
            </div>
            <div class="job-progress-bar"><div class="job-progress-fill" id="job-progress-fill"></div></div>
            <div class="job-stats-grid">
                <div class="job-stat-box"><div class="job-stat-label">Layer</div><div class="job-stat-val" id="stat-layer">0 / 0</div></div>
                <div class="job-stat-box"><div class="job-stat-label">Elapsed</div><div class="job-stat-val" id="stat-elapsed">0m</div></div>
                <div class="job-stat-box"><div class="job-stat-label">Remaining</div><div class="job-stat-val" id="stat-remaining">--</div></div>
            </div>
            <div class="job-actions">
                <button class="btn" id="btn-pause" onclick="sendControl('pause')" disabled>Pause</button>
                <button class="btn" id="btn-resume" onclick="sendControl('resume')" disabled>Resume</button>
                <button class="btn btn-danger" id="btn-stop" onclick="sendControl('stop')" disabled>Cancel Job</button>
            </div>
        </section>

        <!-- Row 1 / Column 2: Control -->
        <section class="card wb3-control">
            <div class="card-header"><h2>Control</h2><span class="tag">Live</span></div>
            <div class="control-grid">
                <button class="control-tile" onclick="startInlineTempEdit('nozzle')">
                    <span class="control-label"><img src="img/icon-nozzle.7a827791.svg">Nozzle</span>
                    <span class="control-value" id="nozzle-value-wrap"><b id="cur-nozzle">0</b><em>/</em><span id="target-nozzle">0</span><small>°C</small></span>
                    <span class="inline-editor" id="nozzle-inline-editor" hidden><input id="nozzle-inline-input" type="number" min="0" max="300"><small>°C</small></span>
                </button>
                <button class="control-tile" onclick="startInlineTempEdit('bed')">
                    <span class="control-label"><img src="img/icon-hotbed.ec6ce931.svg">Heatbed</span>
                    <span class="control-value" id="bed-value-wrap"><b id="cur-bed">0</b><em>/</em><span id="target-bed">0</span><small>°C</small></span>
                    <span class="inline-editor" id="bed-inline-editor" hidden><input id="bed-inline-input" type="number" min="0" max="120"><small>°C</small></span>
                </button>
                <button class="control-tile light-control" onclick="toggleLight()">
                    <span class="control-label"><img src="img/icon-light.efdd1de5.svg">Camera Light</span>
                    <span class="switch-line"><span id="control-light-state">Off</span><span id="control-light-switch" class="toggle-switch"></span></span>
                </button>
                <button class="control-tile" onclick="promptSetFan()">
                    <span class="control-label"><img src="img/icon-fan.02d8a4a3.svg">Model Fan</span>
                    <span class="control-value"><b id="control-fan-value">0%</b></span>
                </button>
                <button class="control-tile speed-control" onclick="cycleSpeedMode()">
                    <span class="control-label"><img src="img/speed-mode.47b8f8db.svg">Speed Mode</span>
                    <span class="control-value"><b id="control-speed-value">Standard</b></span>
                </button>
                <button class="control-tile" onclick="preheat(0, 0)">
                    <span class="control-label"><img src="img/temp.9134d606.svg">Cooldown</span>
                    <span class="control-value"><b>All heaters off</b></span>
                </button>
            </div>
            <div class="control-presets">
                <button onclick="preheat(205,60)">PLA <b>205/60</b></button>
                <button onclick="preheat(235,75)">PETG <b>235/75</b></button>
                <button onclick="preheat(255,90)">ABS <b>255/90</b></button>
            </div>
        </section>

        <!-- Row 1 / Column 3: Device Info -->
        <section class="card wb3-device" id="card-device-info">
            <div class="card-header"><h2>Device Info</h2><span class="tag online-tag">Online</span></div>
            <div class="device-summary">
                <img src="img/Kx.f7c08d6e.png" alt="Kobra X">
                <div><h3>Anycubic Kobra X</h3><span>Model 20030</span><span>Firmware V2.0.1.9</span><span>192.168.1.133</span></div>
            </div>
            <div class="device-selects">
                <label>Buildplate Type<select id="device-buildplate-type" onchange="setBuildplateType(this.value)"><option value="texture_pei">Textured PEI Plate</option><option value="smooth_pei">Smooth PEI Plate</option><option value="low_temp">Low Temperature Plate</option><option value="engineering">Engineering Plate</option></select></label>
                <label>Nozzle Type<select id="device-nozzle-type" onchange="setNozzleType(this.value)"><option value="steel_04">0.4mm Hardened Steel</option><option value="brass_04">0.4mm Brass</option><option value="brass_02">0.2mm Brass</option><option value="steel_06">0.6mm Hardened Steel</option><option value="steel_08">0.8mm Hardened Steel</option></select></label>
            </div>
            <button class="device-cn" onclick="copyCN()">CN 7526-2504-37CE-E5A2 <span>Copy</span></button>
            <div id="device-info-status" class="device-info-status" hidden></div>
        </section>

        <!-- Row 2 / Column 1: Camera -->
        <section class="card wb3-camera">
            <div class="card-header"><h2>Camera</h2><span class="tag">Live View</span></div>
            <div class="camera-container" id="cam-box">
                <video id="cam-video" autoplay muted playsinline></video>
                <div class="camera-placeholder" id="cam-placeholder"><img src="img/video-monitor.aa65df82.png"><span>Camera stream stopped</span></div>
                <div class="camera-top-controls">
                    <button class="icon-button" id="btn-light" onclick="toggleLight()">Light Off</button>
                    <button class="icon-button" id="btn-cam-stream" onclick="toggleCameraStream()">Start Stream</button>
                </div>
                <div class="camera-controls"><button onclick="openCamFull()">Full screen</button><button onclick="startCameraStream()">Reconnect</button></div>
            </div>
        </section>

        <!-- Row 2 / Column 2: Consumables -->
        <section class="card wb3-consumable">
            <div class="card-header">
                <h2>Consumables</h2>
                <div class="card-actions"><button onclick="syncFromSlicerToPrinter()">From Slicer</button><button onclick="syncFilamentsToSlicer()">To Slicer</button></div>
            </div>
            <div class="slots-container" id="slots-container"></div>
            <p class="module-note">Select a slot to edit brand, material, solid, gradient, or luminous color.</p>
        </section>

        <!-- Row 2 / Column 3: Axis -->
        <section class="card wb3-axis" id="card-axis-move">
            <div class="axis-header"><h2>Axis Move</h2><div class="axis-steps"><button id="step-1" onclick="setAxisStep(1,this)">1mm</button><button id="step-15" class="active" onclick="setAxisStep(15,this)">15mm</button><button id="step-50" onclick="setAxisStep(50,this)">50mm</button></div></div>
            <div class="axis-body">
                <div class="axis-actions"><button onclick="homeAll()" title="Home all">⌂</button><button onclick="motorOff()" title="Release motors">✋</button></div>
                <div class="xy-pad">
                    <button class="xy-up" onclick="jog('Y',1)">▲<small>Y</small></button>
                    <button class="xy-left" onclick="jog('X',-1)">◀<small>X</small></button>
                    <button class="xy-home" onclick="homeXY()">⌂</button>
                    <button class="xy-right" onclick="jog('X',1)"><small>X</small>▶</button>
                    <button class="xy-down" onclick="jog('Y',-1)"><small>Y</small>▼</button>
                </div>
                <div class="z-pad"><button onclick="jog('Z',1)">▲<small>Z</small></button><button onclick="homeZ()">⌂</button><button onclick="jog('Z',-1)"><small>Z</small>▼</button></div>
                <div class="axis-divider"></div>
                <div class="extruder-column"><span>Extruder</span><button onclick="extrudeFilament('retract')">↥</button><img src="img/extrusion-dark.006334bf.png"><button onclick="extrudeFilament('extrude')">↧</button></div>
            </div>
            <p id="jog-hint" class="module-note">Axis control enables remote monitoring. Watch nozzle-bed distance.</p>
        </section>

        <!-- Utilities remain below stock grid, spanning all columns -->
        <section class="card wb3-utility">
            <div class="card-header"><h2>Run G-code File</h2><span class="tag">LAN Exec</span></div>
            <div class="gcode-container"><textarea class="gcode-textarea" id="gcode-input" placeholder="; One command per line"></textarea><div class="gcode-bottom"><label><input type="checkbox" id="chk-delete-after" checked> Auto-delete after execution</label><div><button class="btn btn-sm" onclick="clearGcode()">Clear</button><button class="btn btn-primary btn-sm" onclick="runGcodeFile()">Run on Printer</button></div></div><div class="gcode-status" id="gcode-status"></div></div>
        </section>
    </main>

'''

s = s[:start] + new_grid + s[end:]

# CSS appended before </style>; override old 2-column rules and match stock sizing/order.
css = r'''
        /* Workbench V3 stock layout: task/control/device, camera/consumable/axis */
        .wb3-grid {
            display: grid;
            grid-template-columns: minmax(300px, 1.15fr) minmax(290px, .95fr) minmax(310px, 1fr);
            grid-template-areas:
                "task control device"
                "camera consumable axis"
                "utility utility utility";
            gap: 12px;
            align-items: stretch;
        }
        .wb3-grid > .card { margin: 0; min-width: 0; padding: 15px; border-radius: 12px; }
        .wb3-task { grid-area: task; min-height: 286px; }
        .wb3-control { grid-area: control; min-height: 286px; }
        .wb3-device { grid-area: device; min-height: 286px; }
        .wb3-camera { grid-area: camera; min-height: 330px; }
        .wb3-consumable { grid-area: consumable; min-height: 330px; }
        .wb3-axis { grid-area: axis; min-height: 330px; }
        .wb3-utility { grid-area: utility; }
        .wb3-grid .card-header { margin-bottom: 13px; }
        .wb3-grid .card-header h2, .axis-header h2 { font-size: 15px; }
        .job-percent { font-weight: 700; font-size: 15px; color: #1677ff; }
        .online-tag { background: rgba(39,174,96,.12); color: #45d483; }
        .control-grid { display:grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap:8px; }
        .control-tile { min-height:76px; background:#202229; border:1px solid #32343b; border-radius:9px; padding:10px; color:#fff; text-align:left; cursor:pointer; display:flex; flex-direction:column; justify-content:space-between; }
        .control-tile:hover { border-color:#4f8cff; background:#252831; }
        .control-label { display:flex; align-items:center; gap:7px; font-size:11px; color:#b6b8bf; white-space:nowrap; }
        .control-label img { width:17px; height:17px; }
        .control-value { display:flex; align-items:baseline; gap:3px; font-size:17px; }
        .control-value b { color:#fff; font-weight:650; }
        .control-value em { color:#777b85; font-style:normal; }
        .control-value span { color:#858994; }
        .control-value small { font-size:11px; color:#858994; }
        .inline-editor { display:flex; align-items:center; gap:4px; }
        .inline-editor[hidden] { display:none; }
        .inline-editor input { width:75px; background:#111319; color:#fff; border:1px solid #1677ff; border-radius:5px; padding:5px 7px; font-size:16px; }
        .switch-line { display:flex; justify-content:space-between; align-items:center; font-size:13px; }
        .toggle-switch { width:30px; height:16px; background:#4a4d56; border-radius:10px; position:relative; }
        .toggle-switch::after { content:""; width:12px; height:12px; border-radius:50%; background:#fff; position:absolute; left:2px; top:2px; transition:.15s; }
        .toggle-switch.on { background:#1677ff; }
        .toggle-switch.on::after { transform:translateX(14px); }
        .control-presets { display:grid; grid-template-columns:repeat(3,1fr); gap:6px; margin-top:8px; }
        .control-presets button { background:#181a20; border:1px solid #30323a; border-radius:6px; color:#a8abb4; padding:6px; cursor:pointer; font-size:10px; }
        .control-presets button:hover { color:#fff; border-color:#1677ff; }
        .device-summary { display:flex; align-items:center; gap:12px; padding-bottom:11px; border-bottom:1px solid #2d3038; }
        .device-summary img { width:88px; height:74px; object-fit:contain; }
        .device-summary h3 { font-size:14px; margin-bottom:5px; }
        .device-summary span { display:block; font-size:10px; color:#8c909a; line-height:1.55; }
        .device-selects { display:grid; gap:8px; margin-top:11px; }
        .device-selects label { display:grid; grid-template-columns:90px 1fr; gap:8px; align-items:center; color:#9ca0aa; font-size:11px; }
        .device-selects select { background:#1b1d23; color:#fff; border:1px solid #343740; border-radius:6px; padding:6px; font-size:11px; }
        .device-cn { margin-top:8px; width:100%; display:flex; justify-content:space-between; background:transparent; color:#858994; border:0; font-size:10px; cursor:pointer; }
        .device-cn span { color:#4f8cff; }
        .device-info-status { color:#45d483; font-size:10px; margin-top:5px; }
        .wb3-camera .camera-container { min-height:260px; height:calc(100% - 34px); }
        .wb3-camera video { width:100%; height:100%; min-height:260px; object-fit:contain; background:#090a0d; display:none; }
        .wb3-camera .camera-placeholder img { width:76px; opacity:.6; }
        .card-actions { display:flex; gap:5px; }
        .card-actions button { background:#24272e; border:1px solid #383b44; color:#c4c6cc; border-radius:15px; padding:4px 9px; font-size:10px; cursor:pointer; }
        .card-actions button:hover { border-color:#1677ff; color:#fff; }
        .wb3-consumable .slots-container { grid-template-columns:repeat(2,minmax(0,1fr)); gap:8px; }
        .wb3-consumable .slot-card { min-height:112px; padding:12px 8px 9px; }
        .module-note { color:#747883; font-size:10px; text-align:center; margin-top:10px; }
        .axis-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:13px; }
        .axis-steps { background:#1a1c22; border-radius:20px; padding:2px; display:flex; }
        .axis-steps button { border:0; background:transparent; color:#90949e; padding:4px 10px; border-radius:16px; font-size:10px; cursor:pointer; }
        .axis-steps button.active { background:#1677ff; color:white; }
        .axis-body { display:grid; grid-template-columns:40px 130px 42px 1px 68px; align-items:center; justify-content:center; gap:9px; min-height:207px; }
        .axis-actions { display:flex; flex-direction:column; gap:12px; }
        .axis-actions button { width:38px; height:38px; border-radius:50%; background:#22252c; border:1px solid #383b44; color:#4f8cff; font-size:21px; cursor:pointer; }
        .xy-pad { width:130px; height:130px; border-radius:50%; background:#22252c; border:1px solid #3a3d46; position:relative; }
        .xy-pad button { position:absolute; border:0; background:transparent; color:#fff; cursor:pointer; display:flex; gap:3px; align-items:center; justify-content:center; }
        .xy-pad small,.z-pad small { font-size:10px; font-weight:700; }
        .xy-up { top:7px; left:44px; flex-direction:column; }
        .xy-down { bottom:7px; left:44px; flex-direction:column; }
        .xy-left { left:7px; top:52px; }
        .xy-right { right:7px; top:52px; }
        .xy-home { left:43px; top:43px; width:44px; height:44px; border-radius:50%!important; background:#292c34!important; color:#1677ff!important; font-size:21px; z-index:2; }
        .z-pad { height:130px; border-radius:23px; background:#22252c; border:1px solid #3a3d46; display:flex; flex-direction:column; justify-content:space-around; align-items:center; }
        .z-pad button { border:0; background:transparent; color:#fff; cursor:pointer; display:flex; flex-direction:column; align-items:center; }
        .z-pad button:nth-child(2) { width:30px; height:30px; background:#292c34; border-radius:5px; color:#1677ff; font-size:18px; }
        .axis-divider { height:145px; border-left:1px dashed #3b3e47; }
        .extruder-column { height:160px; display:flex; flex-direction:column; align-items:center; justify-content:space-between; }
        .extruder-column span { font-size:10px; color:#a4a7af; }
        .extruder-column img { width:41px; max-height:60px; object-fit:contain; }
        .extruder-column button { width:34px; height:34px; background:#22252c; border:1px solid #383b44; color:#fff; border-radius:7px; font-size:18px; cursor:pointer; }
        .material-controls { display:grid; gap:10px; }
        .finish-editor { display:none; padding:10px; background:#17191f; border:1px solid #30333b; border-radius:8px; margin-top:7px; }
        .finish-editor.active { display:block; }
        .finish-editor-row { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
        .finish-editor label { color:#9da1aa; font-size:11px; }
        .finish-editor input[type=color] { width:34px; height:30px; padding:0; border:0; background:transparent; }
        .gradient-preview,.luminous-preview { height:34px; border-radius:7px; border:1px solid #3a3d46; flex:1; min-width:100px; }
        .gradient-direction { background:#24272e; border:1px solid #383b44; color:#fff; border-radius:5px; padding:5px 8px; }
        .custom-color-row { display:flex; align-items:center; gap:8px; margin-top:9px; }
        .custom-color-row button { background:#24272e; border:1px dashed #4b4e58; color:#bfc2c9; border-radius:6px; padding:5px 9px; cursor:pointer; }
        @media (max-width:1180px) { .wb3-grid { grid-template-columns:1fr 1fr; grid-template-areas:"task control" "camera consumable" "device axis" "utility utility"; } }
        @media (max-width:760px) { .wb3-grid { grid-template-columns:1fr; grid-template-areas:"task" "control" "device" "camera" "consumable" "axis" "utility"; } }
'''
s = s.replace('    </style>', css + '\n    </style>', 1)

# Upgrade finish controls in modal: real editors for solid/gradient/luminous and custom colors.
needle = '''                            <div id="swatch-grid-7x5" style="display: grid; grid-template-columns: repeat(7, 1fr); gap: 9px;">
                                <!-- 35 swatches dynamically generated -->
                            </div>
                            <div style="display: flex; align-items: center; gap: 8px; margin-top: 10px;">
                                <span style="font-size: 11px; color: #9ca3af;">Custom:</span>
                                <input type="color" id="modal-slot-color" style="width: 28px; height: 28px; border: none; border-radius: 50%; cursor: pointer; background: none;" onchange="pickModalColor(this.value)">
                                <span id="modal-color-hex" style="font-family: monospace; font-size: 12px; color: #fff;">#009639</span>
                            </div>'''
replacement = '''                            <div id="swatch-grid-7x5" style="display: grid; grid-template-columns: repeat(7, 1fr); gap: 9px;"></div>
                            <div id="finish-solid-editor" class="finish-editor active">
                                <div class="finish-editor-row"><label>Custom solid</label><input type="color" id="modal-slot-color" value="#009639" onchange="setCustomSolid(this.value)"><span id="modal-color-hex" style="font-family:monospace;font-size:12px;color:#fff">#009639</span><button class="btn btn-sm" onclick="addCustomFinishColor()">+ Add color</button></div>
                            </div>
                            <div id="finish-gradient-editor" class="finish-editor">
                                <div class="finish-editor-row"><label>Gradient colors (2–4)</label><input type="color" id="gradient-color-1" value="#23a3c7" onchange="updateGradientEditor()"><input type="color" id="gradient-color-2" value="#9333ea" onchange="updateGradientEditor()"><input type="color" id="gradient-color-3" value="#ff8da1" onchange="updateGradientEditor()"><input type="color" id="gradient-color-4" value="#fddb27" onchange="updateGradientEditor()"><select id="gradient-direction" class="gradient-direction" onchange="updateGradientEditor()"><option value="180">Vertical</option><option value="90">Horizontal</option></select></div>
                                <div id="gradient-preview" class="gradient-preview"></div><div class="custom-color-row"><button onclick="applyGradientFinish()">Use gradient</button></div>
                            </div>
                            <div id="finish-luminous-editor" class="finish-editor">
                                <div class="finish-editor-row"><label>Day</label><input type="color" id="luminous-day" value="#33a13a" onchange="updateLuminousEditor()"><label>Glow</label><input type="color" id="luminous-night" value="#7fff64" onchange="updateLuminousEditor()"></div>
                                <div id="luminous-preview" class="luminous-preview"></div><div class="custom-color-row"><button onclick="applyLuminousFinish()">Use luminous pair</button></div>
                            </div>
                            <div id="custom-finish-colors" class="custom-color-row"></div>'''
if needle not in s:
    raise SystemExit('modal swatch block not found')
s = s.replace(needle, replacement, 1)

# Add stable state and override duplicated old methods late in script (last definitions win).
js = r'''
        // Workbench V3 finish state and inline controls.
        let selectedFinishType = "solid";
        let selectedColorGroup = ["#23a3c7"];
        let selectedIconType = 0;
        let modelFanPct = 0;
        let customFinishColors = JSON.parse(localStorage.getItem("orcacubic_custom_colors") || "[]");

        function refreshFinishEditors() {
            ["solid","gradient","luminous"].forEach(type => {
                const editor = document.getElementById("finish-" + type + "-editor");
                const tab = document.getElementById("tab-" + type);
                if (editor) editor.classList.toggle("active", selectedFinishType === type);
                if (tab) {
                    tab.style.borderLeft = selectedFinishType === type ? "3px solid #1677ff" : "3px solid transparent";
                    tab.style.color = selectedFinishType === type ? "#fff" : "#6b7280";
                }
            });
            renderCustomFinishColors();
            updateGradientEditor(false);
            updateLuminousEditor(false);
        }

        function switchFinishTab(type) {
            selectedFinishType = type;
            refreshFinishEditors();
        }

        function setCustomSolid(color) {
            selectedFinishType = "solid";
            selectedColorGroup = [color];
            selectedIconType = 0;
            pickModalColor(color);
        }

        function addCustomFinishColor() {
            const color = document.getElementById("modal-slot-color").value;
            if (!customFinishColors.includes(color)) customFinishColors.unshift(color);
            customFinishColors = customFinishColors.slice(0,20);
            localStorage.setItem("orcacubic_custom_colors", JSON.stringify(customFinishColors));
            setCustomSolid(color);
            renderCustomFinishColors();
        }

        function renderCustomFinishColors() {
            const host = document.getElementById("custom-finish-colors");
            if (!host) return;
            host.innerHTML = customFinishColors.length ? '<span style="font-size:11px;color:#8d919b">Custom</span>' : '';
            customFinishColors.forEach(color => {
                const button = document.createElement("button");
                button.title = color;
                button.style.cssText = `width:25px;height:25px;border-radius:50%;padding:0;background:${color};border:1px solid #5a5d66`;
                button.onclick = () => setCustomSolid(color);
                host.appendChild(button);
            });
        }

        function updateGradientEditor(select=true) {
            const colors = [1,2,3,4].map(i => document.getElementById("gradient-color-"+i)?.value).filter(Boolean);
            const angle = Number(document.getElementById("gradient-direction")?.value || 180);
            const preview = document.getElementById("gradient-preview");
            if (preview) preview.style.background = `linear-gradient(${angle}deg, ${colors.join(',')})`;
            if (select) { selectedFinishType="gradient"; selectedColorGroup=colors; selectedIconType=angle===90?2:1; }
        }

        function applyGradientFinish() {
            updateGradientEditor(true);
            pickModalColor(selectedColorGroup[0]);
            refreshFinishEditors();
        }

        function updateLuminousEditor(select=true) {
            const day = document.getElementById("luminous-day")?.value || "#33a13a";
            const night = document.getElementById("luminous-night")?.value || "#7fff64";
            const preview = document.getElementById("luminous-preview");
            if (preview) preview.style.background = `linear-gradient(90deg, ${day} 0 50%, ${night} 50% 100%)`;
            if (select) { selectedFinishType="luminous"; selectedColorGroup=[day,night]; selectedIconType=3; }
        }

        function applyLuminousFinish() {
            updateLuminousEditor(true);
            pickModalColor(selectedColorGroup[0]);
            refreshFinishEditors();
        }

        function openSlotEditModal(slotIdx) {
            currentEditingSlot = slotIdx;
            const f = filaments[slotIdx] || {};
            document.getElementById("modal-slot-title").innerText = "Material Settings";
            document.getElementById("modal-slot-type").value = f.type || "PLA";
            document.getElementById("modal-brand-select").value = f.brand || "Anycubic";
            selectedFinishType = f.finish_type || "solid";
            selectedColorGroup = Array.isArray(f.color_group_hex) && f.color_group_hex.length ? f.color_group_hex : [f.color || "#23a3c7"];
            selectedIconType = Number(f.icon_type || (selectedFinishType === "gradient" ? 1 : selectedFinishType === "luminous" ? 3 : 0));
            const color = selectedColorGroup[0];
            document.getElementById("modal-slot-color").value = color;
            if (selectedFinishType === "gradient") {
                selectedColorGroup.slice(0,4).forEach((c,i) => { const el=document.getElementById("gradient-color-"+(i+1)); if(el) el.value=c; });
                document.getElementById("gradient-direction").value = selectedIconType === 2 ? "90" : "180";
            } else if (selectedFinishType === "luminous") {
                document.getElementById("luminous-day").value = selectedColorGroup[0] || "#33a13a";
                document.getElementById("luminous-night").value = selectedColorGroup[1] || "#7fff64";
            }
            pickModalColor(color);
            refreshFinishEditors();
            document.getElementById("slot-modal").style.display = "flex";
        }

        async function saveSlotFilament() {
            const btn=document.getElementById("btn-save-slot");
            btn.disabled=true; btn.innerText="Saving…";
            const type=document.getElementById("modal-slot-type").value;
            const brand=document.getElementById("modal-brand-select").value;
            try {
                const response=await fetch("http://127.0.0.1:18988/sync_to_printer",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({slots:[{index:currentEditingSlot,type,brand,color:selectedColorGroup[0],finish_type:selectedFinishType,icon_type:selectedIconType,color_group:selectedColorGroup}]})});
                const data=await response.json();
                if(data.status!=="ok") throw new Error(data.message||"Printer rejected setting");
                Object.assign(filaments[currentEditingSlot],{type,brand,color:selectedColorGroup[0],finish_type:selectedFinishType,icon_type:selectedIconType,color_group_hex:[...selectedColorGroup]});
                updateFilamentUI(filaments); closeSlotEditModal(); setTimeout(pollTelemetry,500);
            } catch(e) { alert("Failed to update slot: "+e.message); }
            finally { btn.disabled=false; btn.innerText="Save"; }
        }

        function startInlineTempEdit(type) {
            const wrap=document.getElementById(type+"-value-wrap");
            const editor=document.getElementById(type+"-inline-editor");
            const input=document.getElementById(type+"-inline-input");
            if(!wrap||!editor||!input) return;
            input.value=document.getElementById("target-"+(type==="bed"?"bed":"nozzle")).innerText;
            wrap.hidden=true; editor.hidden=false; input.focus(); input.select();
            const finish=async commit=>{ if(editor.hidden)return; editor.hidden=true; wrap.hidden=false; if(commit){const n=Number(input.value);const max=type==="nozzle"?300:120;if(Number.isFinite(n)&&n>=0&&n<=max)await setTargetTemp(type,n);}};
            input.onkeydown=e=>{if(e.key==="Enter")finish(true);if(e.key==="Escape")finish(false);}; input.onblur=()=>finish(true);
        }

        async function promptSetFan() {
            const raw=prompt("Model fan speed (0–100%)", String(modelFanPct));
            if(raw===null)return; const value=Math.max(0,Math.min(100,Number(raw)));
            if(!Number.isFinite(value)){alert("Enter a value from 0 to 100.");return;}
            const response=await fetch("http://127.0.0.1:18988/control",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({action:"fan",value})});
            const data=await response.json(); if(data.status!=="ok") throw new Error(data.message||"Fan update failed"); modelFanPct=value; document.getElementById("control-fan-value").innerText=Math.round(value)+"%";
        }

        function cycleSpeedMode() { setSpeed(currentSpeedMode >= 4 ? 1 : currentSpeedMode + 1); }
'''
s = s.replace('        setInterval(pollTelemetry, 1500);', js + '\n        setInterval(pollTelemetry, 1500);', 1)

# Update telemetry display for new control widgets, avoid null writes to removed old speed buttons.
s = s.replace('''            if (data.speed_mode !== undefined) {
                for (let i = 1; i <= 4; i++) {
                    const btn = document.getElementById("speed-" + i);
                    if (btn) {
                        if (i === data.speed_mode) btn.classList.add("active");
                        else btn.classList.remove("active");
                    }
                }
            }
            if (data.light !== undefined) {
                lightStatus = data.light;
                document.getElementById("btn-light").innerText = lightStatus === 1 ? "💡 Light: On" : "💡 Light: Off";
            }''','''            if (data.speed_mode !== undefined) {
                currentSpeedMode = Number(data.speed_mode) || 2;
                const speedLabels = {1:"Quiet",2:"Standard",3:"Sport",4:"Ludicrous"};
                const speedValue = document.getElementById("control-speed-value");
                if (speedValue) speedValue.innerText = speedLabels[currentSpeedMode] || "Standard";
            }
            if (data.fan !== undefined) {
                modelFanPct = Number(data.fan) || 0;
                const fanValue = document.getElementById("control-fan-value");
                if (fanValue) fanValue.innerText = Math.round(modelFanPct) + "%";
            }
            if (data.light !== undefined) {
                lightStatus = Number(data.light) || 0;
                const btnLight = document.getElementById("btn-light");
                if (btnLight) btnLight.innerText = lightStatus === 1 ? "Light On" : "Light Off";
                const controlState = document.getElementById("control-light-state");
                const controlSwitch = document.getElementById("control-light-switch");
                if (controlState) controlState.innerText = lightStatus === 1 ? "On" : "Off";
                if (controlSwitch) controlSwitch.classList.toggle("on", lightStatus === 1);
            }''',1)

# Pass fan through telemetry model.
s = s.replace('''                        speed_mode: data.speed_mode,
                        light: data.light,''','''                        speed_mode: data.speed_mode,
                        fan: data.fan,
                        light: data.light,''',1)

# Initialize slot cards, custom controls and saved hardware settings once.
s = s.replace('''        setInterval(pollTelemetry, 1500);
        pollTelemetry();''','''        initSwatchGrid();
        renderCustomFinishColors();
        refreshFinishEditors();
        updateFilamentUI(filaments);
        const savedPlate=localStorage.getItem("anycubic_buildplate"); if(savedPlate) document.getElementById("device-buildplate-type").value=savedPlate;
        const savedNozzle=localStorage.getItem("anycubic_nozzle"); if(savedNozzle) document.getElementById("device-nozzle-type").value=savedNozzle;
        setInterval(pollTelemetry, 1500);
        pollTelemetry();''',1)

# Remove stray initialization accidentally inserted into save success by prior patch.
s = s.replace('''                    pollTelemetry();
            initSwatchGrid();
            const savedPlate = localStorage.getItem('anycubic_buildplate'); if (savedPlate && document.getElementById('device-buildplate-type')) document.getElementById('device-buildplate-type').value = savedPlate;
            const savedNozzle = localStorage.getItem('anycubic_nozzle'); if (savedNozzle && document.getElementById('device-nozzle-type')) document.getElementById('device-nozzle-type').value = savedNozzle;''','''                    pollTelemetry();''')

p.write_text(s, encoding="utf-8")
print("updated", p, len(s))
