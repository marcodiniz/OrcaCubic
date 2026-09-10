import re

src_html = r'X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html'
dst_html = r'X:\code\marcodiniz\OrcaCubic\build\src\Release\resources\web\anycubic\workbench.html'

# We will read update_workbench.py or construct the exact complete file.
# Let's inspect the requirements:
# 1. Device Info card:
#    - Model: Anycubic Kobra X (with image Kx.f7c08d6e.png)
#    - CN Code (with copy icon copy.f57c714c.svg)
#    - Firmware (V 2.0.1.9)
#    - IP (192.168.1.133)
#    - Buildplate Type selector: "Textured PEI Plate", "Low Temperature Plate", "Smooth PEI Plate" (with PEI.e3814fa6.png and Lowtemperature.52fe058f.png)
#    - Nozzle Type selector: "Brass 0.4", "Brass 0.2", "Hardened Steel 0.4", etc.
# 2. Material Settings Modal (from image_b88992.png):
#    - Left column:
#        - 3D Spool preview (using img/consumables.b22bc6e7.svg) dynamically filled with the selected color!
#        - Filament Type label (e.g. PLA)
#        - Hex code (e.g. #009639)
#        - "Feed" and "Unfeed" pill buttons side-by-side
#    - Right column:
#        - "Brand" dropdown (Anycubic, Custom, etc.)
#        - "Material Type" dropdown (PLA, PLA+, PETG, ABS, TPU, ASA, PC, PA, PLA-CF, PETG-CF)
#        - Finish selector column: "Solid" (active with blue vertical indicator bar), "Gradient", "Luminous"
#        - Color Palette grid with 7 columns x 5 rows matching the exact Anycubic swatches
#    - Footer: "Cancel" (gray pill) and "Save" (vivid blue pill)
# 3. Axis Move & Extruder Control card (from image_82ab1b.png):
#    - Left Section (~75% width):
#        - Title: "Axis Move"
#        - Step increment selector: Pill-shaped segmented control ("1mm" active blue pill, "15mm", "50mm")
#        - Auxiliary Left buttons: All-Axes Home circular button, and Motor Unlock / Manual positioning circular button
#        - Center D-Pad ($X/Y$ Circular Jog Dial): 4 quadrants (Y+, Y-, X-, X+) around a center circular Home button
#        - Z-Axis Motion Column: Vertical pill containing Z+ (top), Home Z (center square), Z- (bottom)
#    - Right Section (~25% width, separated by vertical dashed line):
#        - Header: "Extruder"
#        - Top button: Retract (arrow up icon)
#        - Center: Stylized Anycubic printhead / direct-drive extruder graphic (img/extrusion-dark.006334bf.png)
#        - Bottom button: Extrude (arrow down icon)
#    - Bottom Warning notice:
#        - "Axis control will auto-enable remote monitoring. Watch the nozzle-bed distance to avoid collision."
print("Builder script initialized")
