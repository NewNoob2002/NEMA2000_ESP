/* ── Shortcuts ────────────────────────────── */
const $ = (id) => document.getElementById(id);
const TXT = (id, v) => { const e = $(id); if (e) e.textContent = v; };
const VAL = (id, v) => {
  const e = $(id);
  if (!e) return;
  if (e.type === "checkbox") e.checked = (v === true || v === "true" || v === "1");
  else if (e.type === "radio") e.checked = (v === true || v === "true" || v === "1" || String(e.value) === String(v));
  else e.value = v;
};
const GET = (e) => {
  if (e.type === "checkbox") return e.checked ? "true" : "false";
  if (e.type === "radio") return e.checked ? e.value : null;
  return e.value;
};
const SHOW = (id) => { const e = $(id); if (e) e.classList.remove("hidden"); };
const HIDE = (id) => { const e = $(id); if (e) e.classList.add("hidden"); };

/* ── WebSocket ────────────────────────────── */
const WS_URL = `ws://${window.location.hostname}:80/ws`;
let ws = null;
let initialSettings = {};
let reconnectTimer = null;
let reconnectDelay = 2000;

function wsConnect() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
  try {
    ws = new WebSocket(WS_URL);
    ws.onopen = () => {
      TXT("wsStatus", "Connected");
      $("connDot").className = "dot live";
      if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; reconnectDelay = 2000; }
      ws.send("clientReady,true,");
    };
    ws.onclose = () => {
      TXT("wsStatus", "Reconnecting…");
      $("connDot").className = "dot dead";
      scheduleReconnect();
    };
    ws.onerror = () => {
      TXT("wsStatus", "Error");
      $("connDot").className = "dot dead";
    };
    ws.onmessage = (ev) => handleMessage(ev.data);
  } catch (e) {
    console.warn("WS init failed:", e);
    scheduleReconnect();
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    reconnectDelay = Math.min(reconnectDelay * 1.5, 30000);
    wsConnect();
  }, reconnectDelay);
}

/* ── Message parser ───────────────────────── */
function handleMessage(raw) {
  const parts = raw.split(",");
  for (let i = 0; i + 1 < parts.length; i += 2) {
    const key = parts[i];
    const val = parts[i + 1];
    if (!key) continue;

    switch (key) {
      /* ── Live telemetry ── */
      case "utcTime":          TXT("utcTime", val); break;
      case "systemUptime":     TXT("systemUptime", val); break;
      case "satellitesInView": TXT("satellitesInView", val); updateSatBar(); break;
      case "satellitesUsed":   TXT("satellitesUsed", val); updateSatBar(); break;
      case "rtkPosition":      updateFixDisplay(val); break;

      /* ── Firmware ── */
      case "rtkFirmwareVersion":   TXT("rtkFirmwareVersion", val); break;
      case "gnssFirmwareVersion":  TXT("gnssFirmwareVersion", val); break;

      /* ── Identity ── */
      case "platformPrefix":       TXT("platformPrefix", val); break;
      case "hostMessage":          TXT("hostMessage", val); break;

      /* ── ACK from server ── */
      case "ack": break;

      /* ── Profile names ── */
      default:
        if (key.startsWith("profile") && key.endsWith("Name")) {
          TXT(key, val);
        } else if (key === "profileNumber") {
          const radio = document.querySelector(`input[name="profileRadio"][value="${val}"]`);
          if (radio) radio.checked = true;
        } else {
          VAL(key, val);
        }
    }
  }
  refreshUI();
  snapshotSettings();
}

/* ── Fix display logic ────────────────────── */
function updateFixDisplay(text) {
  TXT("rtkPosition", text);
  const badge = $("fixBadge");
  const coords = $("positionCoords");
  if (!badge) return;

  badge.className = "fix-badge";
  if (!text || text.includes("offline")) {
    badge.textContent = "Offline";
    badge.classList.add("nofix");
  } else if (text.includes("RTK Fix")) {
    badge.textContent = "RTK Fix";
    badge.classList.add("rtkfix");
  } else if (text.includes("RTK Float")) {
    badge.textContent = "RTK Float";
    badge.classList.add("float");
  } else if (text.includes("DGPS")) {
    badge.textContent = "DGPS";
    badge.classList.add("dgps");
  } else if (text.includes("Fixed")) {
    badge.textContent = "Fixed";
    badge.classList.add("fix");
  } else if (text.includes("No fix") || text.includes("waiting")) {
    badge.textContent = "No Fix";
    badge.classList.add("nofix");
  } else {
    badge.textContent = text;
  }

  /* Extract coordinates for the detail line */
  const latMatch = text.match(/Lat\s+(-?\d+\.\d+)/);
  const lonMatch = text.match(/Lon\s+(-?\d+\.\d+)/);
  const altMatch = text.match(/Alt\s+(-?\d+\.\d+)/);
  if (latMatch && lonMatch) {
    let coordStr = `Lat ${latMatch[1]}  Lon ${lonMatch[1]}`;
    if (altMatch) coordStr += `  Alt ${altMatch[1]} m`;
    if (coords) coords.textContent = coordStr;
  } else {
    if (coords) coords.textContent = "";
  }
}

/* ── Satellite bar ────────────────────────── */
function updateSatBar() {
  const used = parseInt($("satellitesUsed")?.textContent) || 0;
  const view = parseInt($("satellitesInView")?.textContent) || 0;
  const bar = $("satBar");
  if (!bar) return;
  const pct = view > 0 ? Math.min(100, (used / Math.max(view, 1)) * 100) : 0;
  bar.style.width = `${pct}%`;
  bar.style.background = used >= 12 ? "var(--success)" : used >= 6 ? "var(--warning)" : "var(--primary)";
}

/* ── UI refresh helpers ───────────────────── */
function refreshUI() {
  /* Base mode toggle */
  const isFixed = $("baseTypeFixed")?.checked;
  if ($("surveyInCfg")) isFixed ? HIDE("surveyInCfg") : SHOW("surveyInCfg");
  if ($("fixedCfg"))    isFixed ? SHOW("fixedCfg")    : HIDE("fixedCfg");

  /* Coordinate type toggle */
  const isGeo = $("fixedBaseCoordinateTypeGeo")?.checked;
  if ($("ecefCfg")) isGeo ? HIDE("ecefCfg") : SHOW("ecefCfg");
  if ($("geoCfg"))  isGeo ? SHOW("geoCfg")  : HIDE("geoCfg");

  /* Factory defaults button */
  const chk = $("enableFactoryDefaults");
  const btn = $("factoryDefaults");
  if (chk && btn) btn.disabled = !chk.checked;
}

function snapshotSettings() {
  initialSettings = {};
  document.querySelectorAll("input, select").forEach((el) => {
    if (!el.id || el.type === "file") return;
    const v = GET(el);
    if (v !== null) initialSettings[el.id] = v;
  });
}

/* ── Build profile radio buttons ──────────── */
function buildProfileRadios() {
  const container = $("profileList");
  if (!container) return;
  for (let i = 0; i < 8; i++) {
    const label = document.createElement("label");
    label.className = "radio-item";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = "profileRadio";
    input.value = String(i);
    if (i === 0) input.checked = true;
    const span = document.createElement("span");
    span.id = `profile${i}Name`;
    span.textContent = `Profile ${i + 1}`;
    label.appendChild(input);
    label.appendChild(span);
    container.appendChild(label);
  }
}

/* ── Panel collapse ───────────────────────── */
function setupPanels() {
  document.querySelectorAll(".panel-hdr").forEach((btn) => {
    btn.addEventListener("click", () => {
      const target = $(btn.dataset.target);
      if (!target) return;
      target.classList.toggle("open");
      const arrow = btn.querySelector(".panel-arrow");
      if (arrow) arrow.textContent = target.classList.contains("open") ? "▴" : "▾";
    });
  });
}

/* ── Base / coordinate listeners ──────────── */
function setupBaseToggles() {
  $("baseTypeSurveyIn")?.addEventListener("change", refreshUI);
  $("baseTypeFixed")?.addEventListener("change", refreshUI);
  $("fixedBaseCoordinateTypeECEF")?.addEventListener("change", refreshUI);
  $("fixedBaseCoordinateTypeGeo")?.addEventListener("change", refreshUI);
  $("enableFactoryDefaults")?.addEventListener("change", refreshUI);
}

/* ── Validation ───────────────────────────── */
function validateNum(id, min, max, msg) {
  const el = $(id);
  const errEl = $(`${id}Error`);
  if (!el || el.value === "") { if (errEl) errEl.textContent = ""; return true; }
  const v = Number(el.value);
  const ok = Number.isFinite(v) && v >= min && v <= max;
  if (errEl) errEl.textContent = ok ? "" : msg;
  return ok;
}

function validateAll() {
  let ok = true;
  ok = validateNum("measurementRateHz", 0.00012, 10, "0.00012 – 10 Hz") && ok;
  ok = validateNum("minCN0", 0, 90, "0 – 90 dBHz") && ok;
  ok = validateNum("observationSeconds", 60, 600, "60 – 600 s") && ok;
  ok = validateNum("observationPositionAccuracy", 1, 5, "1 – 5 m") && ok;
  ok = validateNum("antennaPhaseCenter", -200, 200, "-200 – 200 mm") && ok;
  ok = validateNum("antennaHeightM", -15, 15, "-15 – 15 m") && ok;
  return ok;
}

/* ── Changed-settings CSV ─────────────────── */
function changedCSV() {
  let csv = "";
  document.querySelectorAll("input, select").forEach((el) => {
    if (!el.id || el.type === "file") return;
    const v = GET(el);
    if (v === null) return;
    if (initialSettings[el.id] !== v) {
      csv += `${el.id},${v},`;
      initialSettings[el.id] = v;
    }
  });
  const active = document.querySelector('input[name="profileRadio"]:checked');
  if (active && initialSettings["profileNumber"] !== active.value) {
    csv += `profileNumber,${active.value},`;
    initialSettings["profileNumber"] = active.value;
  }
  return csv;
}

/* ── Actions ──────────────────────────────── */
function saveConfig() {
  if (!validateAll()) return;
  const csv = changedCSV();
  if (csv && ws && ws.readyState === WebSocket.OPEN) ws.send(csv);
}

function exitConfig() {
  saveConfig();
  if (ws && ws.readyState === WebSocket.OPEN) ws.send("exitAndReset,true,");
  HIDE("mainApp");
  SHOW("resetInProcess");
}

function btnResetProfile() {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send("resetProfile,true,");
  TXT("profileMsg", "Profile reset requested.");
}

function deleteProfile() {
  const active = document.querySelector('input[name="profileRadio"]:checked');
  const num = active ? active.value : "0";
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(`deleteProfile,${num},`);
  TXT("profileMsg", "Profile delete requested.");
}

function profileUploadWait() {
  const input = $("submitProfileFile");
  if (!input?.files?.length) return;
  const file = input.files[0];
  const reader = new FileReader();
  reader.onload = () => {
    const data = encodeURIComponent(reader.result);
    const active = document.querySelector('input[name="profileRadio"]:checked');
    const num = active ? active.value : "0";
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(`uploadProfile,${num},profileUploadName,${encodeURIComponent(file.name)},profileUploadData,${data},`);
    }
    TXT("profileMsg", "Profile upload requested.");
  };
  reader.onerror = () => TXT("profileMsg", "Profile upload read error.");
  reader.readAsText(file);
}

function resetToFactoryDefaults() {
  if (!$("enableFactoryDefaults")?.checked) return;
  if (ws && ws.readyState === WebSocket.OPEN) ws.send("factoryDefaultReset,true,");
  TXT("factoryDefaultsMsg", "Factory reset requested.");
}

function firmwareUploadWait() {
  const input = $("submitFirmwareFile");
  if (!input?.files?.length) return;
  const file = input.files[0];
  if (!file.name.endsWith(".bin")) {
    TXT("firmwareUploadMsg", "Only .bin files are accepted.");
    return;
  }
  const form = new FormData();
  form.append("binfile", file);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/uploadFirmware");
  xhr.upload.onprogress = (ev) => {
    if (ev.lengthComputable) $("firmwareUploadProgressBar").value = Math.round((ev.loaded / ev.total) * 100);
  };
  xhr.onload = () => {
    if (xhr.status >= 200 && xhr.status < 300) {
      HIDE("mainApp");
      SHOW("firmwareUploadComplete");
    } else {
      TXT("firmwareUploadMsg", xhr.responseText || "Upload failed.");
    }
  };
  xhr.onerror = () => TXT("firmwareUploadMsg", "Upload network error.");
  xhr.send(form);
  TXT("firmwareUploadMsg", "Uploading…");
}

/* ── Init ─────────────────────────────────── */
window.addEventListener("DOMContentLoaded", () => {
  buildProfileRadios();
  setupPanels();
  setupBaseToggles();
  refreshUI();
  snapshotSettings();
  wsConnect();

  /* Periodically refresh WS if stale */
  setInterval(() => {
    if (!ws || ws.readyState !== WebSocket.OPEN) wsConnect();
  }, 10000);
});
