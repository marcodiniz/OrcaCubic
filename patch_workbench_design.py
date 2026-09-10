import sys

workbench_path = r"X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html"
with open(workbench_path, "r", encoding="utf-8") as f:
    text = f.read()

# Let's inspect where cards are placed in column 2
pos_hub = text.find('<!-- ACE Pro Multi-Color Hub -->')
pos_jog = text.find('<!-- Motion & Jog Controls -->')
print('ACE Pro Hub at:', pos_hub)
print('Jog Controls at:', pos_jog)
