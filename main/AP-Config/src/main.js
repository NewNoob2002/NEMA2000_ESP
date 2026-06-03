const gateway = `ws://${window.location.hostname}:80/ws`;
let websocket;
let initialSettings = {};

function ge(id) {
    return document.getElementById(id);
}

function show(id) {
    const element = ge(id);
    if (element) element.classList.remove("hidden");
}

function hide(id) {
    const element = ge(id);
    if (element) element.classList.add("hidden");
}

function setText(id, value) {
    const element = ge(id);
    if (element) element.textContent = value;
}

function setValue(id, value) {
    const element = ge(id);
    if (!element) return;
    if (element.type === "checkbox") {
        element.checked = value === true || value === "true" || value === "1";
    } else if (element.type === "radio") {
        element.checked = value === true || value === "true" || value === "1" || element.value === value;
    } else {
        element.value = value;
    }
}

function getValue(element) {
    if (element.type === "checkbox") return element.checked ? "true" : "false";
    if (element.type === "radio") return element.checked ? element.value : null;
    return element.value;
}

function initWebSocket() {
    try {
        websocket = new WebSocket(gateway);
        websocket.onopen = () => {
            setText("wsStatus", "Connected");
            websocket.send("clientReady,true,");
        };
        websocket.onclose = () => setText("wsStatus", "Disconnected");
        websocket.onerror = () => setText("wsStatus", "Error");
        websocket.onmessage = (message) => parseIncoming(message.data);
    } catch (error) {
        console.log("WebSocket unavailable", error);
    }
}

function parseIncoming(message) {
    const fields = message.split(",");
    for (let index = 0; index + 1 < fields.length; index += 2) {
        const id = fields[index];
        const value = fields[index + 1];

        if (id === "ack") {
            setText("wsLastAck", value);
        } else if (id === "hostMessage") {
            setText("hostData", value);
        } else if (id === "productBrand") {
            ge("pageLogo").src = "singularxyz.png";
        } else if (id === "rtkFirmwareVersion") {
            setText("rtkFirmwareVersion", value);
            setText("rtkFirmwareVersionUpgrade", value);
        } else if (["platformPrefix", "deviceBTID", "gnssFirmwareVersion", "wsStatus", "hostData", "wsLastAck",
                    "utcTime", "systemUptime", "satellitesInView", "satellitesUsed", "rtkPosition"].includes(id)) {
            setText(id, value);
        } else if (id.startsWith("profile") && id.endsWith("Name")) {
            setText(id, value);
        } else if (id === "profileNumber") {
            const radio = document.querySelector(`input[name="profileRadio"][value="${value}"]`);
            if (radio) radio.checked = true;
        } else {
            setValue(id, value);
        }
    }
    refreshDependentControls();
    saveInitialSettings();
}

function saveInitialSettings() {
    initialSettings = {};
    document.querySelectorAll("input, select").forEach((element) => {
        if (!element.id || element.type === "file") return;
        const value = getValue(element);
        if (value !== null) initialSettings[element.id] = value;
    });
}

function changedSettingsCSV() {
    let csv = "";
    document.querySelectorAll("input, select").forEach((element) => {
        if (!element.id || element.type === "file") return;
        const value = getValue(element);
        if (value === null) return;
        if (initialSettings[element.id] !== value) {
            csv += `${element.id},${value},`;
            initialSettings[element.id] = value;
        }
    });

    const activeProfile = document.querySelector('input[name="profileRadio"]:checked');
    if (activeProfile && initialSettings.profileNumber !== activeProfile.value) {
        csv += `profileNumber,${activeProfile.value},`;
        initialSettings.profileNumber = activeProfile.value;
    }
    return csv;
}

function saveConfig() {
    if (!validateConfig()) return;
    const csv = changedSettingsCSV();
    if (csv && websocket && websocket.readyState === WebSocket.OPEN) websocket.send(csv);
}

function exitConfig() {
    saveConfig();
    if (websocket && websocket.readyState === WebSocket.OPEN) websocket.send("exitAndReset,true,");
    hide("mainPage");
    show("resetInProcess");
}

function btnResetProfile() {
    if (websocket && websocket.readyState === WebSocket.OPEN) websocket.send("resetProfile,true,");
    setText("resetProfileMsg", "Profile reset requested.");
}

function selectedProfileNumber() {
    const activeProfile = document.querySelector('input[name="profileRadio"]:checked');
    return activeProfile ? activeProfile.value : "0";
}

function deleteProfile() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send(`deleteProfile,${selectedProfileNumber()},`);
    }
    setText("resetProfileMsg", "Profile delete requested.");
}

function profileUploadWait() {
    const input = ge("submitProfileFile");
    if (!input.files || input.files.length === 0) return;

    const file = input.files[0];
    const reader = new FileReader();
    reader.onload = () => {
        const encodedProfile = encodeURIComponent(reader.result);
        if (websocket && websocket.readyState === WebSocket.OPEN) {
            websocket.send(`uploadProfile,${selectedProfileNumber()},profileUploadName,${encodeURIComponent(file.name)},profileUploadData,${encodedProfile},`);
        }
        setText("profileUploadMsg", "Profile upload requested.");
    };
    reader.onerror = () => setText("profileUploadMsg", "Profile upload failed.");
    reader.readAsText(file);
}

function resetToFactoryDefaults() {
    if (!ge("enableFactoryDefaults").checked) return;
    if (websocket && websocket.readyState === WebSocket.OPEN) websocket.send("factoryDefaultReset,true,");
    setText("factoryDefaultsMsg", "Factory reset requested.");
}

function firmwareUploadWait() {
    const input = ge("submitFirmwareFile");
    if (!input.files || input.files.length === 0) return;

    const file = input.files[0];
    if (!file.name.endsWith(".bin")) {
        setText("firmwareUploadMsg", "Firmware must be a .bin file.");
        return;
    }

    const form = new FormData();
    form.append("binfile", file);

    const request = new XMLHttpRequest();
    request.open("POST", "/uploadFirmware");
    request.upload.onprogress = (event) => {
        if (!event.lengthComputable) return;
        ge("firmwareUploadProgressBar").value = Math.round((event.loaded / event.total) * 100);
    };
    request.onload = () => {
        if (request.status >= 200 && request.status < 300) {
            hide("mainPage");
            show("firmwareUploadComplete");
        } else {
            setText("firmwareUploadMsg", request.responseText || "Firmware upload failed.");
        }
    };
    request.onerror = () => setText("firmwareUploadMsg", "Firmware upload failed.");
    request.send(form);
    setText("firmwareUploadMsg", "Uploading firmware...");
}

function validateNumber(id, min, max, message) {
    const element = ge(id);
    const error = ge(`${id}Error`);
    if (!element || element.value === "") {
        if (error) error.textContent = "";
        return true;
    }
    const value = Number(element.value);
    const valid = Number.isFinite(value) && value >= min && value <= max;
    if (error) error.textContent = valid ? "" : message;
    return valid;
}

function validateConfig() {
    let valid = true;
    valid = validateNumber("measurementRateHz", 0.00012, 10, "Must be between 0.00012 and 10.") && valid;
    valid = validateNumber("minCN0", 0, 90, "Must be between 0 and 90.") && valid;
    valid = validateNumber("observationSeconds", 60, 600, "Must be between 60 and 600.") && valid;
    valid = validateNumber("observationPositionAccuracy", 1, 5, "Must be between 1 and 5.") && valid;
    valid = validateNumber("antennaPhaseCenter", -200, 200, "Must be between -200 and 200.") && valid;
    valid = validateNumber("antennaHeightM", -15, 15, "Must be between -15 and 15.") && valid;
    return valid;
}

function setupCollapseButtons() {
    document.querySelectorAll(".section-toggle").forEach((button) => {
        button.addEventListener("click", () => {
            const target = ge(button.dataset.target);
            if (!target) return;
            target.classList.toggle("open");
            button.querySelector(".caret").textContent = target.classList.contains("open") ? "^" : "v";
        });
    });
}

function refreshDependentControls() {
    if (ge("baseTypeFixed").checked) {
        hide("surveyInConfig");
        show("fixedConfig");
    } else {
        show("surveyInConfig");
        hide("fixedConfig");
    }

    if (ge("fixedBaseCoordinateTypeGeo").checked) {
        hide("ecefConfig");
        show("geodeticConfig");
    } else {
        show("ecefConfig");
        hide("geodeticConfig");
    }

    ge("factoryDefaults").disabled = !ge("enableFactoryDefaults").checked;
}

function setupBaseControls() {
    const updateBaseMode = () => {
        if (ge("baseTypeFixed").checked) {
            hide("surveyInConfig");
            show("fixedConfig");
        } else {
            show("surveyInConfig");
            hide("fixedConfig");
        }
    };
    const updateCoordinateMode = () => {
        if (ge("fixedBaseCoordinateTypeGeo").checked) {
            hide("ecefConfig");
            show("geodeticConfig");
        } else {
            show("ecefConfig");
            hide("geodeticConfig");
        }
    };

    ge("baseTypeSurveyIn").addEventListener("change", updateBaseMode);
    ge("baseTypeFixed").addEventListener("change", updateBaseMode);
    ge("fixedBaseCoordinateTypeECEF").addEventListener("change", updateCoordinateMode);
    ge("fixedBaseCoordinateTypeGeo").addEventListener("change", updateCoordinateMode);
    refreshDependentControls();
}

function setupMeasurementRateSync() {
    ge("measurementRateHz").addEventListener("change", () => {});
}

function setupSystemControls() {
    ge("enableFactoryDefaults").addEventListener("change", () => {
        ge("factoryDefaults").disabled = !ge("enableFactoryDefaults").checked;
    });
}

window.addEventListener("DOMContentLoaded", () => {
    setupCollapseButtons();
    setupBaseControls();
    setupMeasurementRateSync();
    setupSystemControls();
    saveInitialSettings();
    initWebSocket();
});
