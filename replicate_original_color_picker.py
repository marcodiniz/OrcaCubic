from pathlib import Path

p=Path(r"X:\code\marcodiniz\OrcaCubic\resources\web\anycubic\workbench.html")
s=p.read_text(encoding="utf-8")
start=s.index('        // Workbench V3 finish state and inline controls.')
end=s.index('        function startInlineTempEdit(type)',start)
new=r'''        // Original Anycubic LAN color catalog + persisted user colors.
        let selectedFinishType = "solid";
        let selectedColorGroup = ["#23a3c7"];
        let selectedIconType = 0;
        let selectedCatalogKey = "";
        let modelFanPct = 0;
        let activeColorSection = "solid";
        const COLOR_STORAGE_KEY = "orca_ac_lan_user_color_groups_v1";
        const TYPE_TO_GROUP = {solid:"flat_color",gradient:"gradient_color",luminous:"luminous_color"};
        const TYPE_TO_PROTOCOL = {solid:1,gradient:2,luminous:3};

        function rgbaToHex(value) {
            if (!Array.isArray(value) || value.length < 3) return "#afafaf";
            return "#" + value.slice(0,3).map(v=>Math.max(0,Math.min(255,Number(v)||0)).toString(16).padStart(2,"0")).join("");
        }

        function normalizeGroup(group) {
            return (Array.isArray(group)?group:[]).filter(v=>Array.isArray(v)&&v.length>=3).map(v=>[Number(v[0]),Number(v[1]),Number(v[2]),v[3]==null?255:Number(v[3])]);
        }

        function readUserColors() {
            try { const parsed=JSON.parse(localStorage.getItem(COLOR_STORAGE_KEY)||"[]"); return Array.isArray(parsed)?parsed:[]; } catch(e) { return []; }
        }

        function writeUserColors(items) { localStorage.setItem(COLOR_STORAGE_KEY,JSON.stringify(items)); }

        function originalColorGroups() {
            const base=Array.isArray(window.ORCACUBIC_COLOR_GROUPS)?JSON.parse(JSON.stringify(window.ORCACUBIC_COLOR_GROUPS)):[];
            const byKey=new Map(base.map(g=>[g.key,g]));
            readUserColors().forEach(item=>{ const key=TYPE_TO_GROUP[item.finish_type]||TYPE_TO_GROUP.solid; const group=byKey.get(key); if(group){ if(!Array.isArray(group.color))group.color=[]; group.color.push(item); } });
            return base;
        }

        function colorItemStyle(item,type) {
            const colors=normalizeGroup(item.color_group).map(rgbaToHex);
            if(type==="gradient") return `linear-gradient(${Number(item.icon_type)===2?90:180}deg,${colors.join(',')})`;
            if(type==="luminous") return `linear-gradient(90deg,${colors[0]||'#afafaf'} 0 50%,${colors[1]||colors[0]||'#7fff64'} 50% 100%)`;
            if(normalizeGroup(item.color_group)[0]?.[3]===0) return "repeating-conic-gradient(#858585 0 25%,#fff 0 50%) 50%/8px 8px";
            return colors[0]||"#afafaf";
        }

        function itemCatalogKey(item,type,index) { return `${type}-${index}-${item.id||0}-${item.name||''}`; }

        function renderOriginalColorPicker() {
            const groups=originalColorGroups();
            ["solid","gradient","luminous"].forEach(type=>{
                const container=document.getElementById(type+"-color-grid"); if(!container)return; container.innerHTML="";
                const group=groups.find(g=>g.key===TYPE_TO_GROUP[type]);
                const list=group&&Array.isArray(group.color)?group.color:[];
                list.forEach((item,index)=>{
                    const button=document.createElement("button");
                    const key=itemCatalogKey(item,type,index);
                    button.className="original-color-dot"+(type==="luminous"?" luminous":"")+(selectedCatalogKey===key?" selected":"");
                    button.title=item.name||`${type} color`;
                    button.style.background=colorItemStyle(item,type);
                    if(type==="luminous")button.style.setProperty("--night-glow",rgbaToHex(normalizeGroup(item.color_group)[1]||normalizeGroup(item.color_group)[0]));
                    button.onclick=()=>selectCatalogColor(item,type,key);
                    container.appendChild(button);
                });
                const count=list.filter(item=>Number(item.type)===1).length;
                if(count<20){const add=document.createElement("button");add.className="original-color-add";add.title=`Add custom ${type} color`;add.innerText="+";add.onclick=()=>openCustomColorDialog(type);container.appendChild(add);}
            });
        }

        function scrollToColorSection(type) {
            activeColorSection=type;
            document.querySelectorAll(".original-color-tabs button").forEach(b=>b.classList.toggle("active",b.id==="tab-"+type));
            document.getElementById("color-section-"+type)?.scrollIntoView({behavior:"smooth",block:"nearest"});
        }

        function selectCatalogColor(item,type,key) {
            const group=normalizeGroup(item.color_group);
            if(!group.length)return;
            selectedFinishType=type;
            selectedColorGroup=group.map(rgbaToHex);
            selectedIconType=Number(item.icon_type)||({gradient:1,luminous:3}[type]||0);
            selectedCatalogKey=key;
            activeColorSection=type;
            applySpoolFinish();
            renderOriginalColorPicker();
            scrollToColorSection(type);
        }

        function applySpoolFinish() {
            const wound=document.getElementById("spool-wound-fill"); if(!wound)return;
            wound.style.filter="none";
            if(selectedFinishType==="gradient"&&selectedColorGroup.length>=2){
                const stops=[0,1,2,3]; stops.forEach((idx)=>{const stop=document.getElementById("spool-stop-"+idx);const color=selectedColorGroup[Math.min(idx,selectedColorGroup.length-1)];if(stop&&color){stop.setAttribute("stop-color",color);stop.setAttribute("offset",`${Math.round(idx/3*100)}%`);}});
                const gradient=document.getElementById("spool-dynamic-gradient");if(gradient){gradient.setAttribute("x2",selectedIconType===2?"1":"0");gradient.setAttribute("y2",selectedIconType===2?"0":"1");}wound.setAttribute("fill","url(#spool-dynamic-gradient)");
            }else{wound.setAttribute("fill",selectedColorGroup[0]||"#afafaf");if(selectedFinishType==="luminous"&&selectedColorGroup[1])wound.style.filter=`drop-shadow(0 0 5px ${selectedColorGroup[1]})`;}
            const label=selectedFinishType==="gradient"?`Gradient: ${selectedColorGroup.join(" → ")}`:selectedFinishType==="luminous"?`Luminous: ${selectedColorGroup.join(" / ")}`:(selectedColorGroup[0]||"").toUpperCase();
            const hex=document.getElementById("modal-spool-hex");if(hex)hex.innerText=label;
        }

        function openCustomColorDialog(type) {
            activeColorSection=type;
            document.getElementById("custom-color-title").innerText={solid:"Add Solid Color",gradient:"Add Gradient Color",luminous:"Add Luminous Color"}[type];
            document.getElementById("custom-solid-controls").hidden=type!=="solid";
            document.getElementById("custom-gradient-controls").hidden=type!=="gradient";
            document.getElementById("custom-luminous-controls").hidden=type!=="luminous";
            document.getElementById("custom-color-modal").style.display="flex";
            updateCustomPreview();
        }

        function closeCustomColorDialog(){document.getElementById("custom-color-modal").style.display="none";}

        function updateCustomPreview(){
            const preview=document.getElementById("custom-color-preview"); if(!preview)return;
            if(activeColorSection==="solid"){
                const color=document.getElementById("modal-slot-color").value;preview.style.background=color;document.getElementById("modal-color-hex").innerText=color.toUpperCase();
            }else if(activeColorSection==="gradient"){
                const count=Number(document.getElementById("gradient-count").value||2);const inputs=[1,2,3,4].map(i=>document.getElementById("gradient-color-"+i));inputs.forEach((el,i)=>el.style.display=i<count?"inline-block":"none");const colors=inputs.slice(0,count).map(el=>el.value);const angle=Number(document.getElementById("gradient-direction").value||180);preview.style.background=`linear-gradient(${angle}deg,${colors.join(',')})`;
            }else{
                const day=document.getElementById("luminous-day").value,night=document.getElementById("luminous-night").value;preview.style.background=`linear-gradient(90deg,${day} 0 50%,${night} 50% 100%)`;preview.style.boxShadow=`0 0 12px ${night}`;
            }
        }

        function saveCustomColorDialog(){
            let colors=[],icon=0;
            if(activeColorSection==="solid")colors=[document.getElementById("modal-slot-color").value];
            else if(activeColorSection==="gradient"){const count=Number(document.getElementById("gradient-count").value||2);colors=[1,2,3,4].slice(0,count).map(i=>document.getElementById("gradient-color-"+i).value);icon=Number(document.getElementById("gradient-direction").value)===90?2:1;}
            else{colors=[document.getElementById("luminous-day").value,document.getElementById("luminous-night").value];icon=3;}
            const rgba=colors.map(hex=>{const h=hex.slice(1);return[parseInt(h.slice(0,2),16),parseInt(h.slice(2,4),16),parseInt(h.slice(4,6),16),255];});
            const items=readUserColors();const item={id:`lan-user-${Date.now().toString(36)}-${Math.random().toString(36).slice(2,9)}`,name:"",type:1,color_type:TYPE_TO_PROTOCOL[activeColorSection],finish_type:activeColorSection,icon_type:icon,color_group:rgba};items.push(item);writeUserColors(items);
            closeCustomColorDialog();renderOriginalColorPicker();
            const group=originalColorGroups().find(g=>g.key===TYPE_TO_GROUP[activeColorSection]);const index=group.color.length-1;selectCatalogColor(group.color[index],activeColorSection,itemCatalogKey(group.color[index],activeColorSection,index));
        }

        function openSlotEditModal(slotIdx) {
            currentEditingSlot=slotIdx;const f=filaments[slotIdx]||{};
            document.getElementById("modal-slot-title").innerText="Material Settings";
            document.getElementById("modal-slot-type").value=f.type||"PLA";
            document.getElementById("modal-brand-select").value=f.brand||"Anycubic";
            selectedFinishType=f.finish_type||"solid";
            selectedColorGroup=Array.isArray(f.color_group_hex)&&f.color_group_hex.length?f.color_group_hex:[f.color||"#23a3c7"];
            selectedIconType=Number(f.icon_type)||({gradient:1,luminous:3}[selectedFinishType]||0);
            selectedCatalogKey="";
            applySpoolFinish();renderOriginalColorPicker();scrollToColorSection(selectedFinishType);
            document.getElementById("slot-modal").style.display="flex";
        }

        async function saveSlotFilament() {
            const btn=document.getElementById("btn-save-slot");btn.disabled=true;btn.innerText="Saving…";
            const type=document.getElementById("modal-slot-type").value,brand=document.getElementById("modal-brand-select").value;
            try{const response=await fetch("http://127.0.0.1:18988/sync_to_printer",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({slots:[{index:currentEditingSlot,type,brand,color:selectedColorGroup[0],finish_type:selectedFinishType,icon_type:selectedIconType,color_group:selectedColorGroup}]})});const data=await response.json();if(data.status!=="ok")throw new Error(data.message||"Printer rejected setting");Object.assign(filaments[currentEditingSlot],{type,brand,color:selectedColorGroup[0],finish_type:selectedFinishType,icon_type:selectedIconType,color_group_hex:[...selectedColorGroup]});updateFilamentUI(filaments);closeSlotEditModal();setTimeout(pollTelemetry,500);}catch(e){alert("Failed to update slot: "+e.message);}finally{btn.disabled=false;btn.innerText="Save";}
        }

'''
s=s[:start]+new+s[end:]
s=s.replace('''        initSwatchGrid();
        renderCustomFinishColors();
        refreshFinishEditors();''','''        renderOriginalColorPicker();''')
p.write_text(s,encoding="utf-8")
print("rewrote original color picker",len(s))
