"use strict";
(()=>{
const concepts=JSON.parse(document.getElementById("concept-data").textContent);
const byId=new Map(concepts.map(c=>[c.id,c]));
const state={mode:"overview",selected:"quinby",left:"quinby",right:"oddly"};
const $=id=>document.getElementById(id);
const escapeHTML=value=>String(value).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
function image(c,cls="preview"){return '<button type="button" class="'+cls+'" data-zoom="'+c.id+'" aria-label="Увеличить '+c.name+'"><img src="'+c.image+'" alt="'+c.name+': логотип, значок и концепт интерфейса" loading="eager" width="1586" height="992"></button>'}
function init(){
$("overview").innerHTML=concepts.map(c=>'<article class="concept-card"><div class="card-top"><span class="card-num">'+String(c.number).padStart(2,"0")+'</span><h2>'+c.name+'</h2><span class="card-territory">'+c.territory+'</span></div><button type="button" class="preview" data-open="'+c.id+'" aria-label="Рассмотреть '+c.name+'"><img src="'+c.image+'" alt="'+c.name+': знак и приложение" loading="eager" width="1586" height="992"></button><div class="card-bottom"><p>'+c.idea+'</p><div class="card-actions"><span>'+c.ru+' · '+c.name+' Player</span><button type="button" data-pair="'+c.id+'">Сравнить ↗</button></div></div></article>').join("");
$("name-strip").innerHTML=concepts.map(c=>'<button type="button" data-open="'+c.id+'" aria-pressed="false">'+c.name+'</button>').join("");
const options=concepts.map(c=>'<option value="'+c.id+'">'+String(c.number).padStart(2,"0")+' · '+c.name+'</option>').join("");
$("left-select").innerHTML=options;$("right-select").innerHTML=options;
render();
}
function renderDetail(){
const c=byId.get(state.selected);
$("detail-territory").textContent=c.territory;
$("detail-name").textContent=c.name;
$("position").textContent=c.number+" / "+concepts.length;
$("detail-image").src=c.image;
$("detail-image").alt=c.name+": визуальное направление для плеера";
$("detail-image-button").dataset.zoom=c.id;
$("detail-image-button").setAttribute("aria-label","Увеличить "+c.name);
$("name-strip").querySelectorAll("button").forEach(b=>b.setAttribute("aria-pressed",String(b.dataset.open===c.id)));
$("detail-text").innerHTML='<div><span class="eyebrow">Имя и идея</span><p class="pronunciation">'+c.ru+'</p><p>'+escapeHTML(c.idea)+'</p><p class="risk">Пример в речи: «Я пользуюсь '+c.name+' Player».</p></div><div><span class="eyebrow">Что работает</span><p>'+escapeHTML(c.review.good)+'</p><p><b>'+escapeHTML(c.review.verdict)+'</b></p></div><div><span class="eyebrow">Что требует решения</span><p>'+escapeHTML(c.review.weak)+'</p><p class="risk">'+escapeHTML(c.risk)+'</p></div>';
}
function renderCompare(){
$("left-select").value=state.left;$("right-select").value=state.right;
$("compare-grid").innerHTML=[state.left,state.right].map(id=>{const c=byId.get(id);return '<article class="compare-item">'+image(c)+'<h3>'+c.name+' <span class="eyebrow">'+c.ru+' · '+c.territory+'</span></h3><p>'+escapeHTML(c.idea)+'</p><p><b>'+escapeHTML(c.review.good)+'</b></p><p>'+escapeHTML(c.review.weak)+'</p><p class="risk">'+escapeHTML(c.risk)+'</p></article>'}).join("");
}
function render(){
["overview","detail","compare"].forEach(mode=>$(mode).hidden=state.mode!==mode);
document.querySelectorAll("[data-mode]").forEach(b=>b.setAttribute("aria-pressed",String(b.dataset.mode===state.mode)));
if(state.mode==="detail")renderDetail();
if(state.mode==="compare")renderCompare();
$("status").textContent=state.mode==="overview"?"10 направлений":state.mode==="detail"?byId.get(state.selected).name+" · "+byId.get(state.selected).territory:byId.get(state.left).name+" / "+byId.get(state.right).name;
}
function openConcept(id){state.selected=id;state.mode="detail";render();$("detail").scrollIntoView({block:"start",behavior:"auto"})}
function zoom(id){const c=byId.get(id);$("dialog-title").textContent=c.name+" · "+c.territory;$("dialog-image").src=c.image;$("dialog-image").alt=c.name+": полный визуальный лист";$("image-dialog").showModal()}
document.addEventListener("click",event=>{
const button=event.target.closest("button");if(!button)return;
if(button.dataset.mode){state.mode=button.dataset.mode;render()}
else if(button.dataset.open)openConcept(button.dataset.open);
else if(button.dataset.zoom)zoom(button.dataset.zoom);
else if(button.dataset.pair){state.left=button.dataset.pair;if(state.right===state.left)state.right=concepts.find(c=>c.id!==state.left).id;state.mode="compare";render();$("compare").scrollIntoView({block:"start",behavior:"auto"})}
});
$("prev").addEventListener("click",()=>{const i=concepts.findIndex(c=>c.id===state.selected);state.selected=concepts[(i+concepts.length-1)%concepts.length].id;render()});
$("next").addEventListener("click",()=>{const i=concepts.findIndex(c=>c.id===state.selected);state.selected=concepts[(i+1)%concepts.length].id;render()});
$("left-select").addEventListener("change",event=>{state.left=event.target.value;if(state.right===state.left)state.right=concepts.find(c=>c.id!==state.left).id;render()});
$("right-select").addEventListener("change",event=>{state.right=event.target.value;if(state.left===state.right)state.left=concepts.find(c=>c.id!==state.right).id;render()});
$("dialog-close").addEventListener("click",()=>$("image-dialog").close());
$("image-dialog").addEventListener("click",event=>{if(event.target===$("image-dialog"))$("image-dialog").close()});
init();
window.__brandGallery={concepts,state,openConcept};
})();

