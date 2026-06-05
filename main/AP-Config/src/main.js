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
let profileFiles = [];
let activeProfileFile = "";

function wsConnect() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
  try {
    ws = new WebSocket(WS_URL);
    ws.onopen = () => {
      TXT("wsStatus", "Connected");
      $("connDot").className = "dot live";
      if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; reconnectDelay = 2000; }
      ws.send("clientReady,true,");
      refreshProfiles();
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
      case "satellitesInView": TXT("satellitesInView", val); updateSatBar(); break;
      case "satellitesUsed":   TXT("satellitesUsed", val); updateSatBar(); break;
      case "rtkPosition":      updateFixDisplay(val); break;

      /* ── Firmware ── */
      case "rtkFirmwareVersion":   TXT("rtkFirmwareVersion", val); break;
      case "gnssFirmwareVersion":  TXT("gnssFirmwareVersion", val); break;

      /* ── Identity ── */
      case "platformPrefix":       TXT("platformPrefix", val); break;
      case "hostMessage":          TXT("hostMessage", val); break;

      /* ── Profile files ── */
      case "profileListStatus":     TXT("profileMsg", val === "ok" ? "Profile list refreshed." : val); break;
      case "profileCurrent":        TXT("profileCurrent", val || "—"); break;
      case "profileActiveFile":     activeProfileFile = val; break;
      case "profileFileCount":      renderProfileFiles(parseInt(val) || 0); break;
      case "profileActionStatus":   TXT("profileMsg", val); break;

      /* ── ACK from server ── */
      case "ack": break;

      /* ── Profile names ── */
      default:
        if (key.match(/^profileFile\d+Name$/)) {
          const idx = parseInt(key.replace("profileFile", "").replace("Name", ""));
          profileFiles[idx] = profileFiles[idx] || {};
          profileFiles[idx].name = val;
        } else if (key.match(/^profileFile\d+Size$/)) {
          const idx = parseInt(key.replace("profileFile", "").replace("Size", ""));
          profileFiles[idx] = profileFiles[idx] || {};
          profileFiles[idx].size = parseInt(val) || 0;
        } else if (key.startsWith("profile") && key.endsWith("Name")) {
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

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KB`;
}

function profileUrl(path, name) {
  return `${path}?name=${encodeURIComponent(name)}`;
}

function renderProfileFiles(count) {
  const list = $("profileFileList");
  if (!list) return;
  list.innerHTML = "";

  const files = profileFiles.slice(0, count).filter((file) => file && file.name);
  if (!files.length) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = "No profile files found.";
    list.appendChild(empty);
    profileFiles = [];
    return;
  }

  files.forEach((file) => {
    const row = document.createElement("div");
    row.className = "profile-file";
    if (file.name === activeProfileFile) row.classList.add("active");

    const meta = document.createElement("div");
    meta.className = "profile-file-meta";
    const name = document.createElement("strong");
    name.textContent = file.name;
    const size = document.createElement("span");
    size.textContent = `${formatBytes(file.size)}${file.name === activeProfileFile ? " · next boot" : ""}`;
    meta.appendChild(name);
    meta.appendChild(size);

    const actions = document.createElement("div");
    actions.className = "profile-file-actions";

    const download = document.createElement("button");
    download.type = "button";
    download.className = "btn btn-ghost";
    download.textContent = "Download";
    download.onclick = () => { window.location.href = profileUrl("/profile/download", file.name); };

    const activate = document.createElement("button");
    activate.type = "button";
    activate.className = "btn btn-accent";
    activate.textContent = "Activate";
    activate.disabled = file.name === activeProfileFile;
    activate.onclick = () => activateProfile(file.name);

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "btn btn-ghost btn-danger-text";
    remove.textContent = "Delete";
    remove.onclick = () => deleteProfile(file.name);

    actions.appendChild(download);
    actions.appendChild(activate);
    actions.appendChild(remove);
    row.appendChild(meta);
    row.appendChild(actions);
    list.appendChild(row);
  });

  profileFiles = [];
}

function applyProfileList(data) {
  TXT("profileMsg", data.status === "ok" ? "Profile list refreshed." : data.status);
  TXT("profileCurrent", data.current || "—");
  activeProfileFile = data.active || "";
  profileFiles = Array.isArray(data.files) ? data.files : [];
  renderProfileFiles(profileFiles.length);
}

function requestProfileAction(path, name, doneMessage) {
  return fetch(profileUrl(path, name), { method: "POST" })
    .then((response) => {
      if (!response.ok) return response.text().then((text) => { throw new Error(text || "Request failed"); });
      TXT("profileMsg", doneMessage);
      return refreshProfiles();
    })
    .catch((error) => TXT("profileMsg", error.message));
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

function refreshProfiles() {
  TXT("profileMsg", "Refreshing profile files…");
  return fetch("/profile/list")
    .then((response) => {
      if (!response.ok) return response.text().then((text) => { throw new Error(text || "Profile list failed"); });
      return response.json();
    })
    .then(applyProfileList)
    .catch((error) => TXT("profileMsg", error.message));
}

function deleteProfile(name = activeProfileFile) {
  if (!name) {
    TXT("profileMsg", "Select a profile file first.");
    return;
  }
  TXT("profileMsg", "Profile delete requested.");
  requestProfileAction("/profile/delete", name, "Profile deleted.");
}

function activateProfile(name) {
  if (!name) return;
  TXT("profileMsg", "Profile activation requested.");
  requestProfileAction("/profile/activate", name, "Profile activated for next boot.");
}

function profileUploadWait() {
  const input = $("submitProfileFile");
  if (!input?.files?.length) return;
  const file = input.files[0];
  const xhr = new XMLHttpRequest();
  xhr.open("POST", profileUrl("/profile/upload", file.name));
  xhr.onload = () => {
    if (xhr.status >= 200 && xhr.status < 300) {
      TXT("profileMsg", "Profile uploaded.");
      refreshProfiles();
    } else {
      TXT("profileMsg", xhr.responseText || "Profile upload failed.");
    }
  };
  xhr.onerror = () => TXT("profileMsg", "Profile upload network error.");
  xhr.send(file);
  TXT("profileMsg", "Uploading profile…");
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
