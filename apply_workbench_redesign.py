import re

workbench_path = r"X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html"
release_workbench_path = r"X:\code\marcodiniz\OrcaCubic\build\src\Release\resources\web\anycubic\workbench.html"

with open(workbench_path, "r", encoding="utf-8") as f:
    html = f.read()

# 1. Update Modal HTML with the authentic Anycubic Material Settings Modal (from image_b88992.png)
old_modal_pattern = r'<!-- Filament Slot Edit Modal -->[\s\S]*?<!-- Header -->'
new_modal = '''<!-- Filament Slot Edit Modal (Authentic Anycubic Design) -->
    <div id="slot-modal" class="modal-backdrop" style="display: none;">
        <div class="modal-card" style="max-width: 780px; width: 92%;">
            <div class="modal-header" style="position: relative; justify-content: center; padding: 16px 20px;">
                <h3 id="modal-slot-title" style="font-size: 16px; font-weight: 600; color: #fff; margin: 0;">Material Settings</h3>
                <button class="modal-close" onclick="closeSlotEditModal()" style="position: absolute; right: 18px; top: 14px; font-size: 18px; color: #9ca3af; background: none; border: none; cursor: pointer;">✕</button>
            </div>
            <div class="modal-body" style="display: flex; gap: 24px; padding: 24px;">
                <!-- Left Section: 3D Spool Render & Feed/Unfeed -->
                <div style="width: 220px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 14px;">
                    <div id="modal-spool-container" style="width: 130px; height: 165px; display: flex; align-items: center; justify-content: center;">
                        <svg width="130" height="165" viewBox="0 0 55 70" fill="none" xmlns="http://www.w3.org/2000/svg">
                            <g id="spool-model">
                                <ellipse cx="10.6468" cy="34.9304" rx="10.6468" ry="34.9304" transform="matrix(-1 0 0 1 55 0)" fill="#D1D1D1" fill-opacity="0.25"/>
                                <path d="M44.3515 0.478516C43.1101 0.478516 41.8342 1.30526 40.5957 3.02439C39.3648 4.73202 38.2375 7.23458 37.2818 10.3705C35.3723 16.6345 34.1836 25.3178 34.1836 34.9295C34.1836 44.5422 35.3723 53.2255 37.2818 59.4896C38.2375 62.6254 39.3648 65.128 40.5957 66.8353C41.8342 68.5551 43.1101 69.381 44.3515 69.381C45.5919 69.381 46.8678 68.5542 48.1063 66.8353C49.3372 65.128 50.4655 62.6254 51.4212 59.4896C53.3307 53.2255 54.5184 44.5422 54.5184 34.9295C54.5184 25.3178 53.3307 16.6345 51.4212 10.3705C50.4655 7.23458 49.3372 4.73202 48.1063 3.02439C46.8669 1.30545 45.5919 0.478611 44.3515 0.478516Z" stroke="#9ca3af" stroke-width="1.2"/>
                                <path id="spool-wound-fill" d="M42.4334 3.73242C39.5451 3.73242 8.28125 3.73242 8.28125 3.73242C8.28125 3.73242 13.7817 19.5175 13.7817 35.6481C13.7817 51.7787 8.28125 67.5635 8.28125 67.5635C8.28125 67.5635 40.2324 67.5635 42.4334 67.5635C44.6343 67.5635 50.2968 52.8068 50.2968 35.6481C50.2968 18.4894 45.3216 3.73242 42.4334 3.73242Z" fill="#009639"/>
                                <ellipse cx="10.6468" cy="34.9304" rx="10.6468" ry="34.9304" transform="matrix(-1 0 0 1 21.293 0.138672)" fill="#D1D1D1" fill-opacity="0.25"/>
                                <path d="M10.6445 0.617188C9.40307 0.617188 8.12717 1.44393 6.88865 3.16316C5.65781 4.87145 4.53049 7.37402 3.57477 10.5089C1.66523 16.773 0.476562 25.4563 0.476562 35.069C0.476562 44.6807 1.66523 53.3639 3.57477 59.629C4.53049 62.7639 5.65781 65.2665 6.88865 66.9748C8.12717 68.6936 9.40307 69.5204 10.6445 69.5204C11.8849 69.5204 13.1598 68.6936 14.3993 66.9748C15.6301 65.2665 16.7584 62.7639 17.7141 59.629C19.6237 53.3639 20.8114 44.6807 20.8114 35.069C20.8114 25.4563 19.6237 16.773 17.7141 10.5089C16.7584 7.37402 15.6301 4.87145 14.3993 3.16316C13.1598 1.44413 11.8849 0.617283 10.6445 0.617188Z" stroke="#E5E7EB" stroke-width="1.2"/>
                                <ellipse cx="10.3542" cy="35.0696" rx="4.43619" ry="13.0989" fill="black" fill-opacity="0.18"/>
                                <path d="M6.6875 15.5055L8.71227 10.4393L9.40165 12.1799L8.07059 15.5055H6.6875ZM11.5045 15.5055L11.0015 14.2668H9.28024L9.90025 12.7102H10.3859L9.08517 9.47959L9.78755 7.75L12.8919 15.5055H11.5045Z" fill="#D6AD7A" opacity="0.9"/>
                            </g>
                        </svg>
                    </div>
                    <div style="text-align: center;">
                        <div id="modal-spool-type" style="font-size: 16px; font-weight: 700; color: #fff;">PLA</div>
                        <div id="modal-spool-hex" style="font-size: 12px; color: #9ca3af; font-family: monospace; margin-top: 2px;">#009639</div>
                    </div>
                    <div style="display: flex; gap: 10px; width: 100%;">
                        <button class="btn btn-sm" id="btn-modal-feed" onclick="feedCurrentSlot()" style="flex: 1; border-radius: 20px; background: #262a36; color: #6b7280; font-size: 12px;">Feed</button>
                        <button class="btn btn-sm" id="btn-modal-unfeed" onclick="unfeedCurrentSlot()" style="flex: 1; border-radius: 20px; background: #262a36; color: #6b7280; font-size: 12px;">Unfeed</button>
                    </div>
                </div>

                <!-- Separator -->
                <div style="width: 1px; border-left: 1px dashed #374151;"></div>

                <!-- Right Section: Brand, Material Type & 7-Column Swatches -->
                <div style="flex: 1; display: flex; flex-direction: column; gap: 14px;">
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
                        <div>
                            <label style="font-size: 12px; color: #9ca3af; display: block; margin-bottom: 6px;">Brand</label>
                            <select id="modal-brand-select" class="modal-select" style="width: 100%; color: #f59e0b; font-weight: 600; border-radius: 8px;">
                                <option value="Anycubic">Anycubic</option>
                                <option value="Generic">Generic / Public</option>
                                <option value="Custom">Custom</option>
                            </select>
                        </div>
                        <div>
                            <label style="font-size: 12px; color: #9ca3af; display: block; margin-bottom: 6px;">Material Type</label>
                            <select id="modal-slot-type" class="modal-select" style="width: 100%; border-radius: 8px;" onchange="onModalTypeChange(this.value)">
                                <option value="PLA">PLA</option>
                                <option value="PLA+">PLA+</option>
                                <option value="PETG">PETG</option>
                                <option value="ABS">ABS</option>
                                <option value="TPU">TPU</option>
                                <option value="ASA">ASA</option>
                                <option value="PC">PC</option>
                                <option value="PA">PA (Nylon)</option>
                                <option value="PLA-CF">PLA-CF</option>
                                <option value="PETG-CF">PETG-CF</option>
                                <option value="Other">Other</option>
                            </select>
                        </div>
                    </div>

                    <!-- Category Tab & Swatches -->
                    <div style="display: flex; gap: 14px; margin-top: 4px;">
                        <div style="width: 75px; display: flex; flex-direction: column; gap: 8px; padding-top: 4px;">
                            <div class="finish-tab active" id="tab-solid" onclick="switchFinishTab('solid')" style="padding: 4px 6px; font-size: 13px; font-weight: 600; color: #fff; cursor: pointer; border-left: 3px solid #2563eb;">Solid</div>
                            <div class="finish-tab" id="tab-gradient" onclick="switchFinishTab('gradient')" style="padding: 4px 6px; font-size: 13px; color: #6b7280; cursor: pointer; border-left: 3px solid transparent;">Gradient</div>
                            <div class="finish-tab" id="tab-luminous" onclick="switchFinishTab('luminous')" style="padding: 4px 6px; font-size: 13px; color: #6b7280; cursor: pointer; border-left: 3px solid transparent;">Luminous</div>
                        </div>
                        <div style="flex: 1;">
                            <div id="swatch-grid-7x5" style="display: grid; grid-template-columns: repeat(7, 1fr); gap: 9px;">
                                <!-- 35 swatches dynamically generated -->
                            </div>
                            <div style="display: flex; align-items: center; gap: 8px; margin-top: 10px;">
                                <span style="font-size: 11px; color: #9ca3af;">Custom:</span>
                                <input type="color" id="modal-slot-color" style="width: 28px; height: 28px; border: none; border-radius: 50%; cursor: pointer; background: none;" onchange="pickModalColor(this.value)">
                                <span id="modal-color-hex" style="font-family: monospace; font-size: 12px; color: #fff;">#009639</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="modal-footer" style="padding: 12px 24px; display: flex; justify-content: flex-end; gap: 12px; border-top: 1px solid #2d3345;">
                <button class="btn btn-sm" onclick="closeSlotEditModal()" style="border-radius: 20px; padding: 6px 18px; background: #262a36; color: #fff;">Cancel</button>
                <button class="btn btn-primary btn-sm" id="btn-save-slot" onclick="saveSlotFilament()" style="border-radius: 20px; padding: 6px 20px; background: #2563eb; color: #fff; font-weight: 600;">Save</button>
            </div>
        </div>
    </div>

    <!-- Header -->'''

html = re.sub(old_modal_pattern, new_modal, html)

# 2. Add Device Info Card at the top of Right Column
old_hub_comment = '<!-- ACE Pro Multi-Color Hub -->'
device_info_card = '''<!-- Device Info Card -->
            <div class="card" id="card-device-info">
                <div class="card-header">
                    <h2>📋 Device Info</h2>
                    <span class="tag" style="background: rgba(16, 185, 129, 0.15); color: #34d399;">● Online</span>
                </div>
                <div style="display: flex; gap: 16px; align-items: center; margin-bottom: 12px;">
                    <img src="img/Kx.f7c08d6e.png" alt="Kobra X" style="width: 58px; height: 58px; object-fit: contain; filter: drop-shadow(0 4px 8px rgba(0,0,0,0.5));">
                    <div style="flex: 1;">
                        <div style="font-size: 15px; font-weight: 700; color: var(--text-main);">Anycubic Kobra X</div>
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px 12px; font-size: 11px; margin-top: 4px; color: var(--text-muted);">
                            <div>Model: <strong style="color: #fff;">20030</strong></div>
                            <div>Firmware: <strong style="color: #fff;">V 2.0.1.9</strong></div>
                            <div>IP: <strong style="color: #fff;">192.168.1.133</strong></div>
                            <div>CN: <strong style="color: #fff; cursor: pointer;" onclick="copyCN()" title="Click to copy">7526-2504-37CE... 📋</strong></div>
                        </div>
                    </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; border-top: 1px solid var(--border); padding-top: 10px;">
                    <div>
                        <label style="font-size: 11px; color: var(--text-muted); display: block; margin-bottom: 4px;">Buildplate Type</label>
                        <select id="device-buildplate-type" class="modal-select" style="width: 100%; font-size: 12px; padding: 6px 10px; border-radius: 6px;" onchange="setBuildplateType(this.value)">
                            <option value="texture_pei">Textured PEI Plate</option>
                            <option value="smooth_pei">Smooth PEI Plate</option>
                            <option value="low_temp">Low Temperature Plate</option>
                            <option value="engineering">Engineering Plate</option>
                        </select>
                    </div>
                    <div>
                        <label style="font-size: 11px; color: var(--text-muted); display: block; margin-bottom: 4px;">Nozzle Type</label>
                        <select id="device-nozzle-type" class="modal-select" style="width: 100%; font-size: 12px; padding: 6px 10px; border-radius: 6px;" onchange="setNozzleType(this.value)">
                            <option value="steel_04">0.4mm Hardened Steel</option>
                            <option value="brass_04">0.4mm Brass</option>
                            <option value="brass_02">0.2mm Brass</option>
                            <option value="steel_06">0.6mm Hardened Steel</option>
                            <option value="steel_08">0.8mm Hardened Steel</option>
                        </select>
                    </div>
                </div>
                <div id="device-info-status" style="font-size: 11px; color: var(--primary); margin-top: 6px; text-align: center; display: none;">Configuration saved!</div>
            </div>

            <!-- ACE Pro Multi-Color Hub -->'''

html = html.replace(old_hub_comment, device_info_card, 1)

# 3. Replace Axis Motion & Extrusion Card with the authentic design from Image 2
old_jog_pattern = r'<!-- Motion & Jog Controls -->[\s\S]*?</div>\s*</div>\s*</div>\s*<script>'
new_jog = '''<!-- Axis Move & Extruder Card (Authentic Anycubic Design from image_82ab1b) -->
            <div class="card" id="card-axis-move">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
                    <h2 style="font-size: 15px; font-weight: 700; color: #fff; margin: 0;">Axis Move</h2>
                    <!-- Step Increment Segmented Pill -->
                    <div style="display: flex; background: #181a22; border: 1px solid #374151; border-radius: 20px; padding: 2px;">
                        <button class="axis-step-btn" id="step-1" onclick="setAxisStep(1, this)" style="padding: 3px 14px; border-radius: 16px; border: none; background: transparent; color: #9ca3af; font-size: 11px; font-weight: 600; cursor: pointer;">1mm</button>
                        <button class="axis-step-btn active" id="step-15" onclick="setAxisStep(15, this)" style="padding: 3px 14px; border-radius: 16px; border: none; background: #2563eb; color: #fff; font-size: 11px; font-weight: 700; cursor: pointer;">15mm</button>
                        <button class="axis-step-btn" id="step-50" onclick="setAxisStep(50, this)" style="padding: 3px 14px; border-radius: 16px; border: none; background: transparent; color: #9ca3af; font-size: 11px; font-weight: 600; cursor: pointer;">50mm</button>
                    </div>
                </div>

                <div style="display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 6px 0;">
                    <!-- Left: Motion Controls (~75%) -->
                    <div style="flex: 1; display: flex; align-items: center; justify-content: space-around;">
                        <!-- Aux Buttons: Home All & Release -->
                        <div style="display: flex; flex-direction: column; gap: 16px;">
                            <button class="aux-btn" onclick="homeAll()" title="Home All (XYZ)" style="width: 44px; height: 44px; border-radius: 50%; background: #222634; border: 1px solid #374151; display: flex; align-items: center; justify-content: center; cursor: pointer;">
                                <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#2563eb" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                    <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path>
                                    <polyline points="9 22 9 12 15 12 15 22"></polyline>
                                </svg>
                            </button>
                            <button class="aux-btn" onclick="motorOff()" title="Release Motors" style="width: 44px; height: 44px; border-radius: 50%; background: #222634; border: 1px solid #374151; display: flex; align-items: center; justify-content: center; cursor: pointer;">
                                <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#2563eb" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                    <circle cx="12" cy="12" r="10"></polyline>
                                    <line x1="22" y1="12" x2="18" y2="12"></line>
                                    <line x1="6" y1="12" x2="2" y2="12"></line>
                                    <line x1="12" y1="6" x2="12" y2="2"></line>
                                    <line x1="12" y1="22" x2="12" y2="18"></line>
                                </svg>
                            </button>
                        </div>

                        <!-- Center: X/Y Circular D-Pad Dial -->
                        <div style="width: 146px; height: 146px; border-radius: 50%; background: #1e2230; border: 2px solid #374151; position: relative; overflow: hidden; display: flex; align-items: center; justify-content: center; box-shadow: 0 4px 14px rgba(0,0,0,0.4);">
                            <!-- Y+ Wedge -->
                            <button onclick="jog('Y', 1)" style="position: absolute; top: 0; left: 0; width: 100%; height: 50%; background: transparent; border: none; cursor: pointer; display: flex; flex-direction: column; align-items: center; justify-content: flex-start; padding-top: 10px; color: #fff; z-index: 1;">
                                <span style="font-size: 11px;">▲</span>
                                <span style="font-size: 12px; font-weight: 700;">Y</span>
                            </button>
                            <!-- Y- Wedge -->
                            <button onclick="jog('Y', -1)" style="position: absolute; bottom: 0; left: 0; width: 100%; height: 50%; background: transparent; border: none; cursor: pointer; display: flex; flex-direction: column; align-items: center; justify-content: flex-end; padding-bottom: 10px; color: #fff; z-index: 1;">
                                <span style="font-size: 12px; font-weight: 700;">Y</span>
                                <span style="font-size: 11px;">▼</span>
                            </button>
                            <!-- X- Wedge -->
                            <button onclick="jog('X', -1)" style="position: absolute; top: 0; left: 0; width: 50%; height: 100%; background: transparent; border: none; cursor: pointer; display: flex; align-items: center; justify-content: flex-start; padding-left: 12px; gap: 3px; color: #fff; z-index: 1;">
                                <span style="font-size: 11px;">◀</span>
                                <span style="font-size: 12px; font-weight: 700;">X</span>
                            </button>
                            <!-- X+ Wedge -->
                            <button onclick="jog('X', 1)" style="position: absolute; top: 0; right: 0; width: 50%; height: 100%; background: transparent; border: none; cursor: pointer; display: flex; align-items: center; justify-content: flex-end; padding-right: 12px; gap: 3px; color: #fff; z-index: 1;">
                                <span style="font-size: 12px; font-weight: 700;">X</span>
                                <span style="font-size: 11px;">▶</span>
                            </button>
                            <!-- Center Home XY Hub -->
                            <button onclick="homeXY()" title="Home X/Y" style="width: 46px; height: 46px; border-radius: 50%; background: #282d3e; border: 2px solid #374151; z-index: 10; cursor: pointer; display: flex; align-items: center; justify-content: center;">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#2563eb" stroke-width="2">
                                    <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path>
                                </svg>
                            </button>
                        </div>

                        <!-- Right: Z-Axis Vertical Pill -->
                        <div style="width: 48px; height: 146px; border-radius: 24px; background: #1e2230; border: 2px solid #374151; display: flex; flex-direction: column; align-items: center; justify-content: space-between; padding: 6px 0; box-shadow: 0 4px 14px rgba(0,0,0,0.4);">
                            <button onclick="jog('Z', 1)" title="Z+ Up" style="background: none; border: none; color: #fff; cursor: pointer; display: flex; flex-direction: column; align-items: center;">
                                <span style="font-size: 11px;">▲</span>
                                <span style="font-size: 12px; font-weight: 700;">Z</span>
                            </button>
                            <button onclick="homeZ()" title="Home Z" style="width: 34px; height: 34px; border-radius: 6px; background: #282d3e; border: 1px solid #374151; display: flex; align-items: center; justify-content: center; cursor: pointer;">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#2563eb" stroke-width="2">
                                    <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path>
                                </svg>
                            </button>
                            <button onclick="jog('Z', -1)" title="Z- Down" style="background: none; border: none; color: #fff; cursor: pointer; display: flex; flex-direction: column; align-items: center;">
                                <span style="font-size: 12px; font-weight: 700;">Z</span>
                                <span style="font-size: 11px;">▼</span>
                            </button>
                        </div>
                    </div>

                    <!-- Dashed Divider -->
                    <div style="width: 1px; height: 146px; border-left: 1px dashed #374151;"></div>

                    <!-- Right: Extruder Column (~25%) -->
                    <div style="width: 90px; display: flex; flex-direction: column; align-items: center; justify-content: space-between; height: 146px;">
                        <span style="font-size: 12px; font-weight: 600; color: #9ca3af;">Extruder</span>
                        <button onclick="extrudeFilament('retract')" title="Retract" style="width: 38px; height: 38px; border-radius: 8px; background: #222634; border: 1px solid #374151; display: flex; align-items: center; justify-content: center; cursor: pointer;">
                            <img src="img/extrude-retract.08d0fefd.svg" style="width: 20px; height: 20px;" alt="Retract">
                        </button>
                        <img src="img/extrusion-dark.006334bf.png" style="width: 44px; height: auto;" alt="Hotend">
                        <button onclick="extrudeFilament('extrude')" title="Extrude" style="width: 38px; height: 38px; border-radius: 8px; background: #222634; border: 1px solid #374151; display: flex; align-items: center; justify-content: center; cursor: pointer;">
                            <img src="img/extrude-arrow-white.4b34edee.svg" style="width: 20px; height: 20px;" alt="Extrude">
                        </button>
                    </div>
                </div>

                <div style="font-size: 11px; color: #6b7280; text-align: center; margin-top: 10px;">
                    Axis control will auto-enable remote monitoring. Watch the nozzle-bed distance to avoid collision.
                </div>
            </div>
        </div>
    </div>

    <script>'''

html = re.sub(old_jog_pattern, new_jog, html)

# 4. Add the 35 swatches generator & hardware persistence functions to <script>
js_additions = '''
        // --- 35 Anycubic Swatches Palette ---
        const ANYCUBIC_SWATCHES = [
            // Row 1
            "#111827", "#FFFFFF", "#FAF0E6", "#D1D5DB", "#75787B", "#4B5563", "transparent",
            // Row 2
            "#EF4444", "#E11D48", "#EC4899", "#FF8DA1", "#FDBA74", "#F97316", "#F59E0B",
            // Row 3
            "#FDDB27", "#FACC15", "#854D0E", "#D97706", "#FDE68A", "#009639", "#4D7C0F",
            // Row 4
            "#65A30D", "#84CC16", "#1E3A8A", "#2563EB", "#6366F1", "#23A3C7", "#7DD3FC",
            // Row 5
            "#9333EA", "#8B5CF6", "#C084FC", "#F43F5E", "#06B6D4", "#10B981", "#14B8A6"
        ];

        function initSwatchGrid() {
            const container = document.getElementById("swatch-grid-7x5");
            if (!container) return;
            container.innerHTML = "";
            ANYCUBIC_SWATCHES.forEach(color => {
                const btn = document.createElement("button");
                btn.className = "color-swatch-circle";
                btn.style.width = "26px";
                btn.style.height = "26px";
                btn.style.borderRadius = "50%";
                btn.style.border = "2px solid transparent";
                btn.style.cursor = "pointer";
                btn.style.padding = "0";
                btn.style.transition = "transform 0.15s";
                if (color === "transparent") {
                    btn.style.background = "repeating-conic-gradient(#808080 0% 25%, #fff 0% 50%) 50% / 8px 8px";
                    btn.title = "Transparent / Natural";
                } else {
                    btn.style.background = color;
                    btn.title = color;
                }
                btn.onclick = () => pickModalColor(color);
                container.appendChild(btn);
            });
        }

        function pickModalColor(hex) {
            selectedModalColor = hex;
            const wound = document.getElementById("spool-wound-fill");
            if (wound) {
                if (hex === "transparent") {
                    wound.setAttribute("fill", "#e5e7eb");
                    wound.setAttribute("fill-opacity", "0.35");
                } else {
                    wound.setAttribute("fill", hex);
                    wound.removeAttribute("fill-opacity");
                }
            }
            const hexEl = document.getElementById("modal-spool-hex");
            if (hexEl) hexEl.innerText = hex.toUpperCase();
            const colorHexEl = document.getElementById("modal-color-hex");
            if (colorHexEl) colorHexEl.innerText = hex.toUpperCase();
            const picker = document.getElementById("modal-slot-color");
            if (picker && hex !== "transparent") picker.value = hex;

            document.querySelectorAll(".color-swatch-circle").forEach(c => {
                if (c.title === hex) {
                    c.style.borderColor = "#2563eb";
                    c.style.transform = "scale(1.15)";
                    c.style.boxShadow = "0 0 8px #2563eb";
                } else {
                    c.style.borderColor = "transparent";
                    c.style.transform = "none";
                    c.style.boxShadow = "none";
                }
            });
        }

        function onModalTypeChange(type) {
            const el = document.getElementById("modal-spool-type");
            if (el) el.innerText = type;
        }

        function switchFinishTab(tab) {
            document.querySelectorAll(".finish-tab").forEach(t => {
                t.style.borderLeft = "3px solid transparent";
                t.style.color = "#6b7280";
            });
            const active = document.getElementById("tab-" + tab);
            if (active) {
                active.style.borderLeft = "3px solid #2563eb";
                active.style.color = "#fff";
            }
        }

        function setAxisStep(step, el) {
            jogStep = step;
            document.querySelectorAll(".axis-step-btn").forEach(b => {
                b.style.background = "transparent";
                b.style.color = "#9ca3af";
                b.style.fontWeight = "600";
            });
            if (el) {
                el.style.background = "#2563eb";
                el.style.color = "#fff";
                el.style.fontWeight = "700";
            }
        }

        async function homeXY() {
            try {
                await fetch("http://127.0.0.1:18988/control", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ action: "home", axis: "XY" })
                });
            } catch(e) {}
        }

        async function homeZ() {
            try {
                await fetch("http://127.0.0.1:18988/control", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ action: "home", axis: "Z" })
                });
            } catch(e) {}
        }

        async function feedCurrentSlot() {
            try {
                await fetch("http://127.0.0.1:18988/control", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ action: "feed_filament", slot: currentEditingSlot, type: document.getElementById("modal-slot-type").value })
                });
                alert("Feed command sent for Slot " + (currentEditingSlot + 1));
            } catch(e) { alert("Feed failed: " + e.message); }
        }

        async function unfeedCurrentSlot() {
            try {
                await fetch("http://127.0.0.1:18988/control", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ action: "unfeed_filament", slot: currentEditingSlot })
                });
                alert("Unfeed command sent for Slot " + (currentEditingSlot + 1));
            } catch(e) { alert("Unfeed failed: " + e.message); }
        }

        function setBuildplateType(val) {
            localStorage.setItem("anycubic_buildplate", val);
            const status = document.getElementById("device-info-status");
            if (status) {
                status.innerText = "Buildplate set to " + val;
                status.style.display = "block";
                setTimeout(() => status.style.display = "none", 2500);
            }
        }

        function setNozzleType(val) {
            localStorage.setItem("anycubic_nozzle", val);
            const status = document.getElementById("device-info-status");
            if (status) {
                status.innerText = "Nozzle set to " + val;
                status.style.display = "block";
                setTimeout(() => status.style.display = "none", 2500);
            }
        }

        function copyCN() {
            navigator.clipboard.writeText("7526-2504-37CE-E5A2");
            alert("CN Code copied to clipboard: 7526-2504-37CE-E5A2");
        }
'''

html = html.replace("<script>", "<script>\n" + js_additions, 1)

# Ensure initSwatchGrid() runs on load
html = html.replace("pollTelemetry();", "pollTelemetry();\n            initSwatchGrid();\n            const savedPlate = localStorage.getItem('anycubic_buildplate'); if (savedPlate && document.getElementById('device-buildplate-type')) document.getElementById('device-buildplate-type').value = savedPlate;\n            const savedNozzle = localStorage.getItem('anycubic_nozzle'); if (savedNozzle && document.getElementById('device-nozzle-type')) document.getElementById('device-nozzle-type').value = savedNozzle;", 1)

with open(workbench_path, "w", encoding="utf-8") as f:
    f.write(html)

with open(release_workbench_path, "w", encoding="utf-8") as f:
    f.write(html)

print("Successfully written updated workbench.html to source and build Release dirs!")
