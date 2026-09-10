import os

html_path = r'X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html'
with open(html_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Let's replace the whole workbench HTML with the complete authentic Anycubic Dark design
# containing:
# 1. Device Info (Model Kobra X, CN, Firmware, IP, Buildplate Selector, Nozzle Selector)
# 2. Live Camera (18088 FLV with original styling, buttons, light)
# 3. Active Print Job (authentic progress, speed selector, pause/resume/cancel)
# 4. ACE Pro Multi-Color Hub (authentic 4 slots with spool graphics, status, Sync buttons, and full Material Settings modal)
# 5. Temperatures (interactive Nozzle and Heatbed targets with prompt, presets)
# 6. Axis Move & Extruder (authentic circular D-pad, 1mm/15mm/50mm pill, Home buttons, Extruder column with feed/retract and hotend vector)
# 7. Run G-code File block
print('Read', len(text), 'chars')
