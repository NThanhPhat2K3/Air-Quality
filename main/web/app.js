const modeBadge = document.getElementById("modeBadge");
const linkBadge = document.getElementById("linkBadge");
const currentSsid = document.getElementById("currentSsid");
const apInfo = document.getElementById("apInfo");
const credSource = document.getElementById("credSource");
const runtimeIp = document.getElementById("runtimeIp");

const metricAqi = document.getElementById("metricAqi");
const metricAqiCard = document.getElementById("metricAqiCard");
const metricAqiState = document.getElementById("metricAqiState");
const metricEco2 = document.getElementById("metricEco2");
const metricTvoc = document.getElementById("metricTvoc");
const metricTemp = document.getElementById("metricTemp");
const metricHumidity = document.getElementById("metricHumidity");

const scanList = document.getElementById("scanList");
const scanBtn = document.getElementById("scanBtn");
const enablePortalBtn = document.getElementById("enablePortalBtn");
const wifiForm = document.getElementById("wifiForm");
const ssidInput = document.getElementById("ssidInput");
const passwordInput = document.getElementById("passwordInput");
const saveBtn = document.getElementById("saveBtn");
const statusText = document.getElementById("statusText");

const tabButtons = [...document.querySelectorAll(".tab-btn")];
const tabPanels = [...document.querySelectorAll(".tab-panel")];

const alarmTime = document.getElementById("alarmTime");
const alarmMessage = document.getElementById("alarmMessage");
const alarmSaveBtn = document.getElementById("alarmSaveBtn");
const alarmClearBtn = document.getElementById("alarmClearBtn");
const alarmInfo = document.getElementById("alarmInfo");

const ALARM_STORAGE_KEY = "aqnode_alarm";

let scanInProgress = false;
let alarmTimer = null;

function setStatus(message, type = "") {
  if (!statusText) {
    return;
  }
  statusText.textContent = message || "";
  statusText.className = `status ${type}`.trim();
}

function setBadge(el, text, variant = "") {
  if (!el) return;
  el.textContent = text;
  el.className = `badge ${variant}`.trim();
}

function formatSource(source) {
  if (source === "nvs") return "Saved in device (NVS)";
  if (source === "build") return "Firmware fallback";
  return "Not configured";
}

function ensureUiBindings() {
  const missing = [];
  [
    [modeBadge, "modeBadge"],
    [linkBadge, "linkBadge"],
    [currentSsid, "currentSsid"],
    [apInfo, "apInfo"],
    [credSource, "credSource"],
    [runtimeIp, "runtimeIp"],
    [metricAqi, "metricAqi"],
    [metricAqiCard, "metricAqiCard"],
    [metricAqiState, "metricAqiState"],
    [metricEco2, "metricEco2"],
    [metricTvoc, "metricTvoc"],
    [metricTemp, "metricTemp"],
    [metricHumidity, "metricHumidity"],
    [scanList, "scanList"],
    [scanBtn, "scanBtn"],
    [enablePortalBtn, "enablePortalBtn"],
    [wifiForm, "wifiForm"],
    [ssidInput, "ssidInput"],
    [passwordInput, "passwordInput"],
    [saveBtn, "saveBtn"],
    [alarmTime, "alarmTime"],
    [alarmMessage, "alarmMessage"],
    [alarmSaveBtn, "alarmSaveBtn"],
    [alarmClearBtn, "alarmClearBtn"],
    [alarmInfo, "alarmInfo"],
    [statusText, "statusText"],
  ].forEach(([el, id]) => {
    if (!el) {
      missing.push(id);
    }
  });

  if (missing.length > 0) {
    setStatus(`UI mismatch (${missing.join(", ")}). Please hard refresh page (Ctrl+F5).`, "bad");
    return false;
  }
  return true;
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  const text = await response.text();
  let payload = {};
  try {
    payload = text ? JSON.parse(text) : {};
  } catch {
    throw new Error("Invalid JSON response from device");
  }

  if (!response.ok || payload.ok === false) {
    const message = payload.message || `Request failed: ${response.status}`;
    throw new Error(message);
  }
  return payload;
}

function describeAqi(aqi) {
  if (aqi == null || Number.isNaN(Number(aqi))) {
    return { label: "Waiting", tone: "idle" };
  }

  const level = Number(aqi);
  if (level <= 1) return { label: "Level 1 · Excellent", tone: "good" };
  if (level === 2) return { label: "Level 2 · Good", tone: "good" };
  if (level === 3) return { label: "Level 3 · Moderate", tone: "mid" };
  if (level === 4) return { label: "Level 4 · Poor", tone: "warn" };
  return { label: "Level 5 · Unhealthy", tone: "bad" };
}

function switchTab(tabName) {
  tabButtons.forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.tab === tabName);
  });

  tabPanels.forEach((panel) => {
    panel.classList.toggle("active", panel.dataset.panel === tabName);
  });

  if (tabName === "monitoring") {
    loadTelemetry().catch(() => {});
  }
}

async function loadState() {
  const state = await fetchJson("/api/state");
  setBadge(modeBadge, state.mode === "provisioning" ? "Provisioning" : "Runtime");
  setBadge(linkBadge, state.connected ? "Wi-Fi Connected" : "Offline", state.connected ? "ok" : "warn");

  currentSsid.textContent = state.currentSsid || "(none)";
  apInfo.textContent = `${state.apSsid || "-"} / ${state.apPassword || "-"}`;
  credSource.textContent = formatSource(state.credentialSource);

  if (state.runtimeHost) {
    runtimeIp.textContent = `http://${state.runtimeHost}`;
  } else if (state.runtimeIp) {
    runtimeIp.textContent = `http://${state.runtimeIp}`;
  } else {
    runtimeIp.textContent = "-";
  }

  enablePortalBtn.disabled = !state.canStartPortal;

  if (!ssidInput.value && state.currentSsid) {
    ssidInput.value = state.currentSsid;
  }
}

async function loadTelemetry() {
  try {
    const telemetry = await fetchJson("/api/telemetry");
    const aqiView = describeAqi(telemetry.aqi);
    metricAqi.textContent = telemetry.aqi ?? "-";
    metricAqiState.textContent = aqiView.label;
    metricAqiCard.dataset.tone = aqiView.tone;
    metricEco2.textContent = telemetry.eco2 ?? "-";
    metricTvoc.textContent = telemetry.tvoc ?? "-";
    metricTemp.textContent = telemetry.tempC != null ? `${telemetry.tempC.toFixed(1)}°C` : "-";
    metricHumidity.textContent = telemetry.humidity != null ? `${telemetry.humidity}%` : "-";
  } catch {
    metricAqi.textContent = "-";
    metricAqiState.textContent = "Waiting";
    metricAqiCard.dataset.tone = "idle";
    metricEco2.textContent = "-";
    metricTvoc.textContent = "-";
    metricTemp.textContent = "-";
    metricHumidity.textContent = "-";
  }
}

function renderNetworks(items) {
  scanList.innerHTML = "";

  if (!items || items.length === 0) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = "No visible Wi-Fi found. Try moving closer and scan again.";
    scanList.appendChild(empty);
    return;
  }

  items.forEach((item) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "scan-item";

    const left = document.createElement("span");
    left.className = "scan-main";

    const ssid = document.createElement("span");
    ssid.className = "scan-ssid";
    ssid.textContent = item.ssid;

    const meta = document.createElement("span");
    meta.className = "scan-meta";
    meta.textContent = `${item.auth} | RSSI ${item.rssi} dBm`;

    const secure = document.createElement("span");
    secure.className = "scan-meta";
    secure.textContent = item.secure ? "Locked" : "Open";

    left.appendChild(ssid);
    left.appendChild(meta);
    button.appendChild(left);
    button.appendChild(secure);

    button.addEventListener("click", () => {
      ssidInput.value = item.ssid;
      if (!item.secure) {
        passwordInput.value = "";
      }
      setStatus(`Selected network: ${item.ssid}`, "ok");
    });

    scanList.appendChild(button);
  });
}

async function scanWifi() {
  if (scanInProgress) return;

  scanInProgress = true;
  scanBtn.disabled = true;
  setStatus("Scanning nearby Wi-Fi...", "warn");

  try {
    const result = await fetchJson("/api/scan");
    renderNetworks(result.items || []);
    setStatus(`Found ${result.count ?? 0} networks.`, "ok");
  } catch (error) {
    setStatus(error.message, "bad");
  } finally {
    scanBtn.disabled = false;
    scanInProgress = false;
  }
}

function parseAlarm() {
  try {
    const raw = localStorage.getItem(ALARM_STORAGE_KEY);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

function renderAlarm() {
  const alarm = parseAlarm();
  if (!alarm) {
    alarmInfo.textContent = "No alarm configured.";
    return;
  }

  alarmInfo.textContent = `Alarm set at ${alarm.time} - ${alarm.message || "No message"}`;
  alarmTime.value = alarm.time || "";
  alarmMessage.value = alarm.message || "";
}

function scheduleAlarmCheck() {
  if (alarmTimer) {
    clearInterval(alarmTimer);
  }

  alarmTimer = setInterval(() => {
    const alarm = parseAlarm();
    if (!alarm || !alarm.time) {
      return;
    }

    const now = new Date();
    const hh = String(now.getHours()).padStart(2, "0");
    const mm = String(now.getMinutes()).padStart(2, "0");
    const current = `${hh}:${mm}`;

    if (current === alarm.time && !alarm.firedAt) {
      alert(`AQ Node Alarm: ${alarm.message || "Time to check your device."}`);
      localStorage.setItem(ALARM_STORAGE_KEY, JSON.stringify({ ...alarm, firedAt: Date.now() }));
      renderAlarm();
    }

    if (current !== alarm.time && alarm.firedAt) {
      localStorage.setItem(ALARM_STORAGE_KEY, JSON.stringify({
        time: alarm.time,
        message: alarm.message || "",
      }));
    }
  }, 15000);
}

function bindUiEvents() {
  tabButtons.forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.dataset.tab));
  });

  wifiForm.addEventListener("submit", async (event) => {
    event.preventDefault();

    const ssid = ssidInput.value.trim();
    const password = passwordInput.value;

    if (!ssid) {
      setStatus("SSID cannot be empty.", "bad");
      return;
    }

    saveBtn.disabled = true;
    setStatus("Saving Wi-Fi credentials...", "warn");

    try {
      const body = new URLSearchParams();
      body.set("ssid", ssid);
      body.set("password", password);

      const result = await fetchJson("/api/wifi", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: body.toString(),
      });

      const message = result.message || "Saved. Device restarting.";
      setStatus(message, "ok");
      if (!message.toLowerCase().includes("restart")) {
        saveBtn.disabled = false;
      }
    } catch (error) {
      setStatus(error.message, "bad");
      saveBtn.disabled = false;
    }
  });

  scanBtn.addEventListener("click", scanWifi);

  enablePortalBtn.addEventListener("click", async () => {
    enablePortalBtn.disabled = true;
    setStatus("Enabling setup hotspot...", "warn");

    try {
      const result = await fetchJson("/api/provisioning/start", {
        method: "POST",
      });
      setStatus(result.message || "Setup hotspot is active.", "ok");
      await loadState();
    } catch (error) {
      setStatus(error.message, "bad");
    } finally {
      await loadState().catch(() => {});
    }
  });

  alarmSaveBtn.addEventListener("click", () => {
    const time = alarmTime.value;
    const message = alarmMessage.value.trim();

    if (!time) {
      setStatus("Please set alarm time first.", "bad");
      return;
    }

    localStorage.setItem(
      ALARM_STORAGE_KEY,
      JSON.stringify({
        time,
        message,
      })
    );
    renderAlarm();
    setStatus("Alarm saved on this browser.", "ok");
  });

  alarmClearBtn.addEventListener("click", () => {
    localStorage.removeItem(ALARM_STORAGE_KEY);
    alarmTime.value = "";
    alarmMessage.value = "";
    renderAlarm();
    setStatus("Alarm cleared.", "ok");
  });
}

async function boot() {
  if (!ensureUiBindings()) {
    return;
  }

  bindUiEvents();
  renderAlarm();
  scheduleAlarmCheck();

  try {
    await Promise.all([loadState(), loadTelemetry()]);
  } catch (error) {
    setStatus(error.message, "bad");
  }

  setStatus("Dashboard ready.", "ok");

  setInterval(() => {
    loadState().catch((error) => setStatus(error.message, "bad"));
  }, 5000);

  setInterval(() => {
    loadTelemetry().catch(() => {});
  }, 1000);
}

boot().catch((error) => {
  setStatus(`Boot failed: ${error.message || "unknown error"}`, "bad");
});
