const $ = selector => document.querySelector(selector);
const $$ = selector => [...document.querySelectorAll(selector)];
const api = async (path, options = {}) => {
  const response = await fetch(path, { headers: { "Content-Type": "application/json" }, ...options });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `Error ${response.status}`);
  return data;
};
const post = (path, body) => api(path, { method: "POST", body: JSON.stringify(body) });
const names = { temperature_c: "Temperatura", setpoint_c: "Consigna", light_lux: "Luz ambiente", gas_pct: "Gas", brightness_pct: "Brillo luz", door_open: "Puerta", contact: "Marcos metálicos" };
const icons = { temperature_c: "⌁", setpoint_c: "◎", light_lux: "☼", gas_pct: "≈", brightness_pct: "✺", door_open: "↔", contact: "⛨" };
const fmt = (value, digits = 0) => Number(value).toFixed(digits);
let currentState = null;
let recognition = null;
let cameraStream = null;
let pendingModality = "text";
let toastTimer;
let lastBrailleSeq = null;
let lastNoticeSeq = null;
let lastGasAlarm = false;
let portsLoaded = false;

function toast(message) {
  const el = $("#toast"); el.textContent = message; el.classList.add("show");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove("show"), 2600);
}
function speak(text) {
  if (!("speechSynthesis" in window)) return false;
  window.speechSynthesis.cancel();
  const utterance = new SpeechSynthesisUtterance(text); utterance.lang = "es-CO";
  window.speechSynthesis.speak(utterance);
  return true;
}
function escapeHtml(text) { return String(text).replace(/[&<>"']/g, char => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[char])); }
const isHardware = () => currentState?.mode === "hardware";
// No reconstruir un bloque mientras la persona lo está usando (evita cerrar listas o mover deslizadores).
const busy = selector => { const host = $(selector); return host && host.contains(document.activeElement) && document.activeElement !== document.body; };

function setConnection(online) {
  $("#connection-label").textContent = online ? "Servicio conectado" : "Servicio sin conexión";
  $(".live-dot").classList.toggle("offline", !online);
}
function renderSensors(sensors) {
  const temp = sensors.temperature_c === null || sensors.temperature_c === undefined ? "—" : `${fmt(sensors.temperature_c, 1)} <small>°C</small>`;
  const values = {
    temperature_c: temp,
    setpoint_c: `${fmt(sensors.setpoint_c, 1)} <small>°C</small>`,
    light_lux: `${fmt(sensors.light_lux)} <small>lux</small>`,
    gas_pct: `${fmt(sensors.gas_pct)} <small>%</small>`,
    brightness_pct: `${fmt(sensors.brightness_pct)} <small>%</small>`,
    door_open: sensors.door_open ? "Abierta" : "Cerrada",
    contact: sensors.contact ? "Contacto" : "Sin contacto"
  };
  const sources = currentState?.sources;
  $("#sensor-summary").innerHTML = Object.keys(names).map(key => `<article class="sensor-card"><div class="sensor-top"><span>${names[key]}</span><span class="sensor-symbol">${icons[key]}</span></div><div class="sensor-value">${values[key]}</div><div class="sensor-foot">${sources ? escapeHtml(sources[key]) : "simulación"}</div></article>`).join("");
}
function renderActuators(actuators, manual = {}) {
  if (busy("#actuator-controls")) return;
  const auto = currentState?.control?.mode === "auto";
  const tag = device => auto && manual[device] ? ' <span class="override-tag">MANUAL 60 s</span>' : "";
  const moving = currentState?.control?.curtain_moving ? " · moviendo" : "";
  const select = (device, label, options) => `<select data-device="${device}" aria-label="${label}">${options.map(([value, text]) => `<option value="${value}" ${actuators[device] === value ? "selected" : ""}>${text}</option>`).join("")}</select>`;
  const controls = [
    ["Luz (GPIO2)" + tag("light"), `<input type="range" min="0" max="100" value="${actuators.light}" data-device="light" aria-label="Nivel de luz"><output>${actuators.light}%</output>`],
    ["Ventilador (relé)" + tag("fan"), `<label class="switch-control"><input type="checkbox" data-device="fan" ${actuators.fan ? "checked" : ""}>${actuators.fan ? "Encendido" : "Apagado"}</label>`],
    ["Cortina (paso a paso)" + tag("curtain"), `<span class="curtain-meter" title="${actuators.curtain_pct}% abierta${moving}"><span style="width:${actuators.curtain_pct}%"></span></span>${select("curtain", "Cortina", [["closed", "Cerrada"], ["open", "Abierta"]])}`],
    ["Ventana (servo)" + tag("window"), select("window", "Ventana", [["closed", "Cerrada"], ["open", "Abierta"]])],
    ["Pestillo puerta (servo)", select("latch", "Pestillo", [["locked", "Cerrado"], ["unlocked", "Abierto"]])],
    ["Sirena (buzzer 1)", `<label class="switch-control"><input type="checkbox" data-device="siren" ${actuators.siren ? "checked" : ""}>${actuators.siren ? "Sonando" : "Apagada"}</label>`],
    ["Timbre (buzzer 2)", `<button class="bell-button" id="bell-ring">${actuators.bell ? "Sonando…" : "♪ Tocar"}</button>`]
  ];
  $("#actuator-controls").innerHTML = controls.map(([label, input]) => `<div class="actuator-row"><label>${label}</label><div class="actuator-setting">${input}</div></div>`).join("");
  $$("#actuator-controls input[type=range]").forEach(input => input.addEventListener("input", () => { input.nextElementSibling.value = `${input.value}%`; }));
  $$("#actuator-controls [data-device]").forEach(input => input.addEventListener("change", async () => {
    const value = input.type === "range" ? Number(input.value) : input.type === "checkbox" ? input.checked : input.value;
    input.blur();
    await runCommand(input.dataset.device, value);
  }));
  $("#bell-ring").addEventListener("click", () => runCommand("bell", "ring"));
}
function renderBridge(status = {}) {
  const connected = Boolean(status.connected);
  const receiving = Boolean(status.receiving);
  const via = /^socket:/.test(status.url || "") ? "Wi-Fi" : /^rfc2217:/.test(status.url || "") ? "Wokwi" : "puerto serie (USB o Bluetooth)";
  const stateText = receiving ? `Conectado por ${via} (${status.url}) · recibiendo datos` : connected ? `Puerto ${status.url} abierto · esperando datos del ESP32…` : status.error ? `No conecta: ${status.error} · reintentando` : status.state === "connecting" ? "Conectando…" : "No conectado · la página usa la simulación local";
  $("#bridge-state").textContent = stateText;
  $("#bridge-dot").classList.toggle("connected", receiving);
  $("#bridge-dot").classList.toggle("bridge-error", Boolean(status.error) || (connected && !receiving));
  $("#bridge-description").textContent = receiving
    ? `Última lectura: ${new Date(status.last_seen).toLocaleTimeString("es-CO")}${status.firmware ? ` · ${status.firmware}` : ""}. Los sensores vienen del ESP32.`
    : connected ? "Si no llegan datos, revisa que cargaste el firmware principal (no el de pruebas) y cierra el monitor serie de VS Code: el puerto solo puede abrirlo un programa."
    : "USB: el COM del CH9102. Bluetooth: empareja \"CasaMultimodal\" y usa su COM saliente. Wi-Fi: socket://IP:3333. Wokwi: rfc2217://127.0.0.1:4000.";
  $("#bridge-toggle").textContent = connected || status.state === "connecting" ? "Desconectar" : "Conectar";
  if (!$("#bridge-url").value && status.url) $("#bridge-url").value = status.url;
}
function renderMode(state) {
  const hardware = state.mode === "hardware";
  $("#mode-chip").textContent = hardware ? "ESP32 EN VIVO" : "SIMULACIÓN";
  $("#mode-chip").classList.toggle("hardware", hardware);
  $("#home-badge").textContent = hardware ? "● ESP32 conectado" : "● Simulación";
  $("#footer-mode").textContent = hardware ? `ESP32 conectado (${state.control?.channels || "usb"}${state.control?.ip ? " · " + state.control.ip : ""}). Luz y gas se simulan con potenciómetros.` : "Sin ESP32 conectado: mediciones simuladas.";
  $("#sensor-label").textContent = hardware ? "LECTURAS DEL ESP32" : "EDITA LOS VALORES";
  $("#save-sensors").hidden = hardware;
  $("#actuator-help").textContent = hardware ? "Las órdenes se envían al ESP32; el estado mostrado es el que reporta la placa." : "Sin ESP32 conectado, los cambios solo se aplican a la simulación.";
  $("#rules-state").textContent = hardware ? "ACTIVAS EN EL ESP32" : "REQUIEREN ESP32 O WOKWI";
  const mode = state.control?.mode || "auto";
  $$(".segmented [data-mode]").forEach(button => button.setAttribute("aria-pressed", String(button.dataset.mode === mode)));
  const dip = state.control?.dip_auto === undefined ? "" : ` DIP 1 de la maqueta: ${state.control.dip_auto ? "ON (auto)" : "OFF (manual)"}.`;
  $("#mode-help").textContent = (mode === "auto" ? "Automático: las reglas deciden; una orden manual tiene prioridad durante 60 s." : "Manual: solo tus órdenes mueven luz, ventilador, cortina y ventana. La alarma de gas siempre actúa.") + dip;
}
function renderAlarms(state) {
  const control = state.control || {};
  const alarms = control.alarms || {};
  const messages = [];
  let danger = false, canSilence = false;
  if (alarms.gas) { messages.push(`⚠ ALARMA DE GAS (${fmt(state.sensors.gas_pct)} %): ventilador, ventana y puerta abiertos.${alarms.siren_muted ? " Sirena silenciada." : ""}`); danger = true; canSilence = !alarms.siren_muted; }
  const security = control.security || {};
  if (security.intrusion) { messages.push(`⚠ INTRUSIÓN: ${security.cause || "zona activada"}. Desactiva la seguridad o pasa una tarjeta autorizada.${security.siren_muted ? " Sirena silenciada." : ""}`); danger = true; canSilence = canSilence || !security.siren_muted; }
  else if (security.arming_s > 0) messages.push(`Activando seguridad: ${security.arming_s} s para salir y cerrar la puerta…`);
  if (alarms.door_forced) { messages.push("⚠ Puerta forzada: se abrió con el pestillo cerrado."); danger = true; canSilence = true; }
  if (alarms.door_left_open) messages.push("La puerta quedó abierta después del acceso.");
  if (alarms.temp_sensor === false) messages.push("El sensor de temperatura no responde: revisa el KY-028 (A0 → GPIO32, + → 3V3).");
  if (control.access?.active && !alarms.gas) messages.push(`Acceso concedido · el pestillo se cierra en ${control.access.remaining_s} s (o al cerrar la puerta).`);
  if (control.learning) messages.push("Acerca la tarjeta que quieres autorizar al lector…");
  ["#alarm-banner", "#alarm-banner-home"].forEach(selector => {
    const el = $(selector);
    el.hidden = !messages.length;
    el.innerHTML = `<span>${messages.map(escapeHtml).join(" · ")}</span>${canSilence ? '<button data-silence>Silenciar sirena</button>' : ""}`;
    el.classList.toggle("warn", !danger);
  });
  $$("[data-silence]").forEach(button => button.addEventListener("click", async () => {
    try { currentState = await post("/api/alarm/silence", {}); toast("Sirena silenciada"); } catch (error) { toast(error.message); }
  }));
  const gas = Boolean(alarms.gas);
  if (!gas && lastGasAlarm) toast("Alarma de gas desactivada");
  lastGasAlarm = gas;
}
function renderSecurity(state) {
  const security = state.control?.security || {};
  const panel = $("#security-panel");
  const status = security.intrusion ? "intrusion" : security.arming_s > 0 ? "arming" : security.armed ? "armed" : "off";
  ["armed", "arming", "intrusion"].forEach(name => panel.classList.toggle(name, status === name));
  $("#security-state").textContent = { off: "DESACTIVADO", arming: `ACTIVANDO · ${security.arming_s} s`, armed: "ACTIVADO", intrusion: "¡INTRUSIÓN!" }[status];
  $("#security-toggle").textContent = status === "off" ? "Activar seguridad" : "Desactivar seguridad";
  $("#security-toggle").dataset.armed = String(status === "off");
  $("#security-help").textContent = {
    off: "Desactivado. Al activarlo tienes 10 s para salir; luego, tocar un marco metálico (KY-036) o abrir la puerta dispara la alarma. Se desactiva aquí o con una tarjeta autorizada.",
    arming: "Sal y cierra la puerta. Si la puerta sigue abierta al terminar la cuenta, no se activa.",
    armed: "Vigilando los marcos metálicos (KY-036) y la puerta (reed). Una tarjeta autorizada desactiva la seguridad y abre el pestillo.",
    intrusion: `Intrusión por: ${security.cause || "zona activada"}. La sirena suena hasta desactivar o silenciar.`
  }[status];
}
async function setSecurity(armed) {
  try { currentState = await post("/api/security", { armed }); renderSecurity(currentState); toast(armed ? "Activando modo seguridad…" : "Modo seguridad desactivado"); setTimeout(refreshState, 800); }
  catch (error) { toast(error.message); }
}
function handleNotice(notice) {
  if (!notice) return;
  if (lastNoticeSeq === null) { lastNoticeSeq = notice.seq; return; }
  if (notice.seq === lastNoticeSeq) return;
  lastNoticeSeq = notice.seq;
  const spoken = { intrusion: "Alerta de intrusión. Se detectó contacto o apertura con la seguridad activada.", security_armed: "Modo seguridad activado.", security_disarmed: "Modo seguridad desactivado.", security_arm_failed: "No se pudo activar la seguridad: la puerta está abierta.", doorbell: "Alguien está tocando el timbre.", gas_alarm_on: "Alerta de gas. Se activó la ventilación y se abrió la puerta.", door_forced: "Atención: la puerta se abrió con el pestillo cerrado.", access_granted: "Acceso concedido.", access_denied: "Tarjeta no autorizada." };
  toast(notice.text || "Aviso de la casa");
  if (spoken[notice.kind]) speak(spoken[notice.kind]);
}
function renderSensorControls(sensors) {
  if (busy("#sensor-controls")) return;
  const disabled = isHardware() ? "disabled" : "";
  const range = (id, key, label, min, max, step, unit, digits = 0) => `<div class="range-control"><label for="${id}">${label}</label><output>${sensors[key] === null ? "—" : fmt(sensors[key], digits)} ${unit}</output><input id="${id}" data-sensor="${key}" data-unit=" ${unit}" type="range" min="${min}" max="${max}" step="${step}" value="${sensors[key] ?? min}" ${disabled}></div>`;
  $("#sensor-controls").innerHTML =
    range("sens-temp", "temperature_c", "Temperatura (KY-028)", 10, 40, 0.5, "°C", 1) +
    range("sens-setpoint", "setpoint_c", "Consigna del termostato", 18, 32, 0.5, "°C", 1) +
    range("sens-light", "light_lux", "Luz ambiente", 0, 1000, 10, "lux") +
    range("sens-gas", "gas_pct", "Gas", 0, 100, 1, "%") +
    range("sens-brightness", "brightness_pct", "Brillo de la luz", 0, 100, 1, "%") +
    `<label class="switch-control"><input id="sens-door" type="checkbox" ${sensors.door_open ? "checked" : ""} ${disabled}> Puerta abierta (reed)</label>` +
    `<label class="switch-control"><input id="sens-contact" type="checkbox" ${sensors.contact ? "checked" : ""} ${disabled}> Contacto en marco metálico (KY-036)</label>`;
  $$("#sensor-controls input[type=range]").forEach(input => input.addEventListener("input", () => { input.previousElementSibling.value = `${input.value}${input.dataset.unit}`; }));
}
function renderRfid(state) {
  const control = state.control || {};
  $("#rfid-status").textContent = control.rfid_ok === true ? "LECTOR EN LÍNEA" : control.rfid_ok === false ? "LECTOR SIN RESPUESTA" : "LECTOR";
  const reads = state.rfid_reads || [];
  if (!busy("#rfid-reads")) {
    $("#rfid-reads").innerHTML = reads.length ? reads.map(read => `<div class="rfid-row"><div><code>${escapeHtml(read.uid)}</code><div class="rfid-meta">${new Date(read.timestamp).toLocaleTimeString("es-CO")}${read.simulated ? " · simulada" : ""}</div></div><span class="${read.authorized ? "ok" : "bad"}">${read.authorized ? "Concedido" : "Denegado"}</span>${read.authorized ? "" : `<button data-authorize="${escapeHtml(read.uid)}">Autorizar</button>`}</div>`).join("") : '<p class="helper">Acerca una tarjeta al lector.</p>';
    $$("[data-authorize]").forEach(button => button.addEventListener("click", () => { $("#rfid-uid").value = button.dataset.authorize; $("#rfid-name").focus(); }));
  }
}
async function refreshRfidUsers() {
  try {
    const users = await api("/api/rfid/users");
    $("#rfid-users").innerHTML = users.length ? users.map(user => `<div class="rfid-row"><div><b>${escapeHtml(user.name)}</b><div class="rfid-meta"><code>${escapeHtml(user.uid)}</code></div></div><button data-remove="${escapeHtml(user.uid)}">Quitar</button></div>`).join("") : '<p class="helper">Aún no hay tarjetas autorizadas.</p>';
    $$("[data-remove]").forEach(button => button.addEventListener("click", async () => {
      if (!window.confirm(`¿Quitar la tarjeta ${button.dataset.remove}?`)) return;
      try { await api(`/api/rfid/users/${button.dataset.remove}`, { method: "DELETE" }); await refreshRfidUsers(); toast("Tarjeta eliminada"); } catch (error) { toast(error.message); }
    }));
  } catch { /* el servicio puede estar reiniciándose */ }
}
async function refreshState() {
  try {
    currentState = await api("/api/state"); setConnection(true);
    renderMode(currentState);
    renderSensors(currentState.sensors); renderSensorControls(currentState.sensors);
    renderActuators(currentState.actuators, currentState.control?.manual);
    renderBridge(currentState.bridge); renderAlarms(currentState); renderRfid(currentState); renderSecurity(currentState);
    handleNotice(currentState.notice);
  } catch (error) { setConnection(false); }
}
async function refreshHistory() {
  try {
    const events = await api("/api/events?limit=100");
    const host = $("#history-list");
    if (!events.length) { host.innerHTML = '<div class="empty-history">Todavía no hay eventos. Envía un mensaje para comenzar.</div>'; return; }
    const labels = { ok: "Registrado", auto: "Automático", alerta: "Alerta", rechazado: "Rechazado", bloqueado: "En espera" };
    host.innerHTML = events.map(event => {
      const time = new Date(event.timestamp).toLocaleTimeString("es-CO", { hour: "2-digit", minute: "2-digit", second: "2-digit" });
      const modality = event.modality || ({ actuation: "control", access: "acceso", alarm: "alarma", device: "ESP32" }[event.category] || event.category);
      return `<div class="history-row"><span class="history-event">${escapeHtml(event.content)}</span><span class="history-modality">${escapeHtml(modality)}</span><span class="history-time">${time}</span><span class="status-pill">${escapeHtml(labels[event.status] || event.status)}</span></div>`;
    }).join("");
  } catch { $("#history-list").innerHTML = '<div class="empty-history">No se pudo cargar la actividad.</div>'; }
}
async function loadPorts() {
  try {
    const ports = await api("/api/ports");
    const label = { usb: "USB", bluetooth: "Bluetooth", wifi: "Wi-Fi", wokwi: "Wokwi" };
    $("#port-list").innerHTML = ports.map(port => `<option value="${escapeHtml(port.device)}">${label[port.kind] || ""} · ${escapeHtml(port.description)}</option>`).join("");
    // Preferir el adaptador USB del ESP32 (CH9102, CP210x, CH340).
    const usb = ports.find(port => port.kind === "usb" && /usb|cp210|ch34|ch91|uart|silicon/i.test(port.description)) || ports.find(port => port.kind === "usb");
    if (!$("#bridge-url").value) $("#bridge-url").value = usb ? usb.device : "rfc2217://127.0.0.1:4000";
    portsLoaded = true;
    return ports;
  } catch { return []; }
}

async function runCommand(device, value, modality) {
  try {
    currentState = await post("/api/commands", { device, value, modality });
    renderActuators(currentState.actuators, currentState.control?.manual); await refreshHistory();
    toast(device === "bell" ? "Timbre" : currentState.bridge?.connected ? "Orden enviada al ESP32" : "Orden aplicada en la simulación");
    if (currentState.control?.alarms?.gas && ["fan", "window", "latch"].includes(device)) toast("Alarma de gas activa: la orden se aplicará cuando termine");
  } catch (error) { toast(error.message); await refreshState(); }
}
async function setMode(mode) {
  try { currentState = await post("/api/mode", { mode }); renderMode(currentState); toast(mode === "auto" ? "Modo automático" : "Modo manual"); setTimeout(refreshState, 1200); }
  catch (error) { toast(error.message); }
}
function parseCommand(text) {
  const value = text.toLocaleLowerCase("es").normalize("NFD").replace(/[̀-ͯ]/g, "");
  const on = /\b(encender|enciende|prender|prende|activar|activa)\b/, off = /\b(apagar|apaga|desactivar|desactiva)\b/;
  if (/\b(luz|luces|iluminacion)\b/.test(value) && on.test(value)) return ["light", 80, "Encender luz al 80 %"];
  if (/\b(luz|luces|iluminacion)\b/.test(value) && off.test(value)) return ["light", 0, "Apagar luz"];
  if (/\b(ventilador|ventilacion)\b/.test(value) && on.test(value)) return ["fan", true, "Activar ventilación"];
  if (/\b(ventilador|ventilacion)\b/.test(value) && off.test(value)) return ["fan", false, "Apagar ventilación"];
  if (/\bcortina\b/.test(value) && /\b(abrir|abre|subir|sube)\b/.test(value)) return ["curtain", "open", "Abrir cortina"];
  if (/\bcortina\b/.test(value) && /\b(cerrar|cierra|bajar|baja)\b/.test(value)) return ["curtain", "closed", "Cerrar cortina"];
  if (/\b(puerta|pestillo)\b/.test(value) && /\b(abrir|abre|desbloquear)\b/.test(value)) return ["latch", "unlocked", "Abrir el pestillo de la puerta"];
  if (/\b(puerta|pestillo)\b/.test(value) && /\b(cerrar|cierra|bloquear)\b/.test(value)) return ["latch", "locked", "Cerrar el pestillo de la puerta"];
  if (/\bventana\b/.test(value) && /\b(abrir|abre)\b/.test(value)) return ["window", "open", "Abrir la ventana"];
  if (/\bventana\b/.test(value) && /\b(cerrar|cierra)\b/.test(value)) return ["window", "closed", "Cerrar la ventana"];
  if (/\btimbre\b/.test(value)) return ["bell", "ring", "Tocar el timbre"];
  if (/\b(desactivar|desactiva|desarmar|desarma)\b/.test(value) && /\b(seguridad|alarma)\b/.test(value)) return ["security", false, "Desactivar el modo seguridad"];
  if (/\b(activar|activa|armar|arma)\b/.test(value) && /\b(seguridad|alarma)\b/.test(value)) return ["security", true, "Activar el modo seguridad"];
  if (/\b(silenciar|apagar|apaga)\b/.test(value) && /\b(alarma|sirena)\b/.test(value)) return ["silence", true, "Silenciar la sirena"];
  if (/\bmodo\b/.test(value) && /\bautomatico\b/.test(value)) return ["mode", "auto", "Cambiar a modo automático"];
  if (/\bmodo\b/.test(value) && /\bmanual\b/.test(value)) return ["mode", "manual", "Cambiar a modo manual"];
  return null;
}
async function sendMessage(text, modality = "text") {
  if (!text.trim()) { toast("Escribe o dicta un mensaje primero"); return; }
  try {
    await post("/api/messages", { modality, content: text.trim() });
    $("#message-input").value = "";
    pendingModality = "text";
    const command = parseCommand(text);
    if (command && window.confirm(`¿Quieres ${command[2].toLocaleLowerCase("es")}?`)) {
      if (command[0] === "mode") await setMode(command[1]);
      else if (command[0] === "silence") { currentState = await post("/api/alarm/silence", {}); }
      else if (command[0] === "security") await setSecurity(command[1]);
      else await runCommand(command[0], command[1], modality);
      speak(`Listo: ${command[2]}`);
    } else toast("Mensaje registrado");
    await refreshHistory();
  } catch (error) { toast(error.message); }
}

$$(".tab").forEach(tab => tab.addEventListener("click", () => showView(tab.dataset.view)));
function showView(name) {
  $$(".tab").forEach(tab => tab.classList.toggle("active", tab.dataset.view === name));
  $$(".view").forEach(view => view.classList.toggle("active", view.id === `view-${name}`));
  if (name === "history") refreshHistory();
  if (name === "home") { refreshState(); refreshRfidUsers(); if (!portsLoaded) loadPorts(); }
  window.scrollTo({ top: 0, behavior: "smooth" });
}
$$('[data-goto="home"]').forEach(button => button.addEventListener("click", () => showView("home")));
$("#send-message").addEventListener("click", () => sendMessage($("#message-input").value, pendingModality));
$("#read-message").addEventListener("click", () => {
  const text = $("#message-input").value.trim();
  if (!text) { toast("Escribe un mensaje para escucharlo"); return; }
  if (!speak(text)) toast("Este navegador no ofrece síntesis de voz");
});
$("#message-input").addEventListener("keydown", event => { if ((event.ctrlKey || event.metaKey) && event.key === "Enter") sendMessage(event.currentTarget.value, pendingModality); });
$$('.quick-action').forEach(button => button.addEventListener("click", () => {
  let value = button.dataset.value;
  if (value === "true") value = true; else if (value === "false") value = false; else if (/^\d+$/.test(value)) value = Number(value);
  if (button.dataset.command === "security") setSecurity(true); else runCommand(button.dataset.command, value);
}));
$$(".segmented [data-mode]").forEach(button => button.addEventListener("click", () => setMode(button.dataset.mode)));
$("#security-toggle").addEventListener("click", event => setSecurity(event.currentTarget.dataset.armed !== "false"));

$("#save-sensors").addEventListener("click", async () => {
  const values = Object.fromEntries($$("#sensor-controls [data-sensor]").map(input => [input.dataset.sensor, Number(input.value)]));
  values.door_open = $("#sens-door").checked;
  values.contact = $("#sens-contact").checked;
  try { currentState = await post("/api/sensors", values); renderSensors(currentState.sensors); renderSensorControls(currentState.sensors); await refreshHistory(); toast("Sensores simulados actualizados"); }
  catch (error) { toast(error.message); }
});

$("#bridge-toggle").addEventListener("click", async () => {
  try {
    const status = await api("/api/bridge");
    if (status.connected || status.state === "connecting") { await post("/api/bridge/disconnect", {}); toast("Conexión cerrada"); }
    else { await post("/api/bridge/connect", { url: $("#bridge-url").value.trim() }); toast("Conectando…"); }
    setTimeout(refreshState, 600);
  } catch (error) { toast(error.message); }
});
$("#ports-refresh").addEventListener("click", async () => { const ports = await loadPorts(); toast(`${ports.length} puerto(s) encontrados`); });

const readUid = () => $("#rfid-uid").value.trim().toUpperCase().replace(/[^0-9A-F]/g, "");
$("#rfid-add").addEventListener("click", async () => {
  try { await post("/api/rfid/users", { uid: readUid(), name: $("#rfid-name").value }); $("#rfid-uid").value = ""; $("#rfid-name").value = ""; await refreshRfidUsers(); toast("Tarjeta autorizada"); }
  catch (error) { toast(error.message); }
});
$("#rfid-learn").addEventListener("click", async () => {
  try { await post("/api/rfid/learn", {}); toast("Acerca la tarjeta al lector"); setTimeout(refreshRfidUsers, 8000); }
  catch (error) { toast(error.message); }
});
$("#rfid-simulate").addEventListener("click", async () => {
  const uid = readUid();
  if (!uid) { toast("Escribe un UID para simular la lectura"); return; }
  try { await post("/api/rfid/simulate", { uid }); setTimeout(refreshState, 500); }
  catch (error) { toast(error.message); }
});

const dotValues = new Set();
const brailleMap = new Map([
  ["1", "a"], ["12", "b"], ["14", "c"], ["145", "d"], ["15", "e"], ["124", "f"], ["1245", "g"], ["125", "h"], ["24", "i"], ["245", "j"], ["13", "k"], ["123", "l"], ["134", "m"], ["1345", "n"], ["135", "o"], ["1234", "p"], ["12345", "q"], ["1235", "r"], ["234", "s"], ["2345", "t"], ["136", "u"], ["1236", "v"], ["2456", "w"], ["1346", "x"], ["13456", "y"], ["1356", "z"], ["12456", "ñ"]
]);
function dotKey() { return [...dotValues].sort().join(""); }
function refreshBraillePreview() {
  const key = dotKey(); $("#braille-glyph").textContent = String.fromCodePoint(0x2800 + [...dotValues].reduce((sum, dot) => sum + (1 << (dot - 1)), 0));
  $("#braille-letter").textContent = key ? `${brailleMap.get(key) || "?"} · puntos ${[...dotValues].sort().join("-")}` : "Sin puntos";
}
$$('.braille-dot').forEach(button => button.addEventListener("click", () => {
  const dot = Number(button.dataset.dot);
  if (dotValues.has(dot)) dotValues.delete(dot); else dotValues.add(dot);
  button.setAttribute("aria-pressed", dotValues.has(dot)); refreshBraillePreview();
}));
function clearDots() { dotValues.clear(); $$(".braille-dot").forEach(button => button.setAttribute("aria-pressed", "false")); refreshBraillePreview(); }
$("#braille-add").addEventListener("click", () => {
  const key = dotKey(); if (!key) { toast("Selecciona uno o más puntos"); return; }
  const letter = brailleMap.get(key) || "�"; const output = $("#braille-text");
  if (output.textContent === "Tu texto aparecerá aquí") output.textContent = "";
  output.textContent += letter; clearDots();
});
$("#braille-space").addEventListener("click", () => { const output = $("#braille-text"); if (output.textContent === "Tu texto aparecerá aquí") output.textContent = ""; output.textContent += " "; clearDots(); });
$("#braille-delete").addEventListener("click", () => { const output = $("#braille-text"); output.textContent = output.textContent === "Tu texto aparecerá aquí" ? "Tu texto aparecerá aquí" : output.textContent.slice(0, -1); });
$("#braille-send").addEventListener("click", () => {
  const text = $("#braille-text").textContent;
  if (!text.trim() || text === "Tu texto aparecerá aquí") { toast("Escribe una combinación primero"); return; }
  sendMessage(text, "braille").then(() => { $("#braille-text").textContent = "Tu texto aparecerá aquí"; });
});

$("#speak-button").addEventListener("click", () => {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) { $("#voice-status").textContent = "Este navegador no ofrece reconocimiento de voz. Puedes escribir el mensaje en el campo."; return; }
  if (recognition) { recognition.stop(); recognition = null; $("#speak-button").innerHTML = '<span class="mic-icon">◉</span> Hablar'; return; }
  recognition = new SpeechRecognition(); recognition.lang = "es-CO"; recognition.interimResults = false; recognition.maxAlternatives = 1;
  $("#voice-status").textContent = "Escuchando… al terminar podrás revisar el texto antes de enviarlo.";
  $("#speak-button").textContent = "Detener escucha";
  recognition.onresult = event => { $("#message-input").value = event.results[0][0].transcript; pendingModality = "voice"; $("#voice-status").textContent = "Transcripción lista. Revísala y pulsa Enviar mensaje."; };
  recognition.onerror = event => { $("#voice-status").textContent = `No se pudo reconocer la voz (${event.error}). Puedes escribir el mensaje.`; };
  recognition.onend = () => { recognition = null; $("#speak-button").innerHTML = '<span class="mic-icon">◉</span> Hablar'; };
  recognition.start();
});
$("#message-input").addEventListener("input", () => { if (!recognition) pendingModality = "text"; });

$("#camera-start").addEventListener("click", async () => {
  if (!navigator.mediaDevices?.getUserMedia) { $("#camera-status").textContent = "Este contexto no permite acceso a cámara. Usa HTTPS o localhost y un navegador compatible."; return; }
  try {
    cameraStream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: { ideal: "environment" } }, audio: false });
    const video = $("#camera-video"); video.srcObject = cameraStream; await video.play(); video.classList.add("active");
    $("#camera-placeholder").hidden = true; $("#camera-start").disabled = true; $("#camera-stop").disabled = false;
    $("#camera-status").textContent = "Vista local activa. El video no se envía ni almacena.";
  } catch (error) { $("#camera-status").textContent = `No se pudo abrir la cámara (${error.name}). Revisa permisos del navegador.`; }
});
function stopCamera() {
  cameraStream?.getTracks().forEach(track => track.stop()); cameraStream = null;
  const video = $("#camera-video"); video.pause(); video.srcObject = null; video.classList.remove("active");
  $("#camera-placeholder").hidden = false; $("#camera-start").disabled = false; $("#camera-stop").disabled = true;
  $("#camera-status").textContent = "Cámara detenida. No se han almacenado imágenes.";
}
$("#camera-stop").addEventListener("click", stopCamera);
document.addEventListener("visibilitychange", () => { if (document.hidden && cameraStream) stopCamera(); });
$("#refresh-history").addEventListener("click", refreshHistory);
$(".notice-close").addEventListener("click", () => $(".notice").remove());
function setPhoneView(on) {
  document.body.classList.toggle("phone-view", on);
  $("#phone-toggle").setAttribute("aria-pressed", String(on));
  try { localStorage.setItem("phone-view", on ? "1" : "0"); } catch { /* sin almacenamiento */ }
}
$("#phone-toggle").addEventListener("click", () => setPhoneView(!document.body.classList.contains("phone-view")));
try { if (localStorage.getItem("phone-view") === "1") setPhoneView(true); } catch { /* sin almacenamiento */ }
if (new URLSearchParams(location.search).has("celular")) setPhoneView(true);
if ("serviceWorker" in navigator) navigator.serviceWorker.register("/sw.js").catch(() => {});
refreshState(); refreshHistory(); refreshRfidUsers(); loadPorts();
setInterval(refreshState, 1500);
