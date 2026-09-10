from pathlib import Path
import json
import re

source = Path(r"C:\Users\User\AppData\Local\Temp\anycubic_workbench_extracted\js\src_views_workbench2_WorkbenchV3_vue.js")
target = Path(r"X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\color-groups.js")
text = source.read_text(encoding="utf-8", errors="ignore")
marker = '/***/ "./src/mocks/lanColorGroupList.json"'
start = text.index(marker)
match = re.search(r"JSON\.parse\('(.*?)'\);", text[start:start + 100000], re.S)
if not match:
    raise SystemExit("Original Anycubic LAN color catalog not found")
raw = match.group(1).encode().decode("unicode_escape")
data = json.loads(raw)["data"]
target.write_text("window.ORCACUBIC_COLOR_GROUPS = " + json.dumps(data, separators=(",", ":")) + ";\n", encoding="utf-8")
for group in data:
    print(group["key"], len(group["color"]))
print(target, target.stat().st_size)
