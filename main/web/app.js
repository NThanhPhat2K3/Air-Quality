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
const hiddenInput = document.getElementById("hiddenInput");
const saveBtn = document.getElementById("saveBtn");
const statusText = document.getElementById("statusText");
const memoryFileInput = document.getElementById("memoryFileInput");
const memoryPreview = document.getElementById("memoryPreview");
const memoryLoadBtn = document.getElementById("memoryLoadBtn");
const memoryClearBtn = document.getElementById("memoryClearBtn");
const memoryStatus = document.getElementById("memoryStatus");
const memoryInfo = document.getElementById("memoryInfo");
const memorySelection = document.getElementById("memorySelection");

const tabButtons = [...document.querySelectorAll(".tab-btn")];
const tabPanels = [...document.querySelectorAll(".tab-panel")];

const alarmTime = document.getElementById("alarmTime");
const alarmMessage = document.getElementById("alarmMessage");
const alarmSaveBtn = document.getElementById("alarmSaveBtn");
const alarmClearBtn = document.getElementById("alarmClearBtn");
const alarmInfo = document.getElementById("alarmInfo");

const ALARM_STORAGE_KEY = "aqnode_alarm";
const MEMORY_WIDTH = 160;
const MEMORY_HEIGHT = 128;
const MEMORY_BYTES = MEMORY_WIDTH * MEMORY_HEIGHT * 2;

let scanInProgress = false;
let alarmTimer = null;
let wifiFormDirty = false;
let memoryRgb565Buffer = null;
let memoryDraftDirty = false;
let memoryDraft = null;
let memoryPointerDrag = null;

function markWifiFormDirty() {
  wifiFormDirty = true;
}

function resetWifiFormDirty() {
  wifiFormDirty = false;
}

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
    [hiddenInput, "hiddenInput"],
    [saveBtn, "saveBtn"],
    [memoryFileInput, "memoryFileInput"],
    [memoryPreview, "memoryPreview"],
    [memoryLoadBtn, "memoryLoadBtn"],
    [memoryClearBtn, "memoryClearBtn"],
    [memoryStatus, "memoryStatus"],
    [memoryInfo, "memoryInfo"],
    [memorySelection, "memorySelection"],
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
    return;
  }

  if (tabName === "memory") {
    loadMemoryPhotoState().catch(() => {});
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
  if (!wifiFormDirty) {
    hiddenInput.checked = Boolean(state.hiddenSsid);
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

function setMemoryStatus(message, type = "") {
  if (!memoryStatus) return;
  memoryStatus.textContent = message || "";
  memoryStatus.className = `hint ${type}`.trim();
}

function setMemorySelection(message) {
  if (!memorySelection) return;
  memorySelection.textContent = message || "";
}

function cleanupMemoryDraftSource() {
  if (!memoryDraft?.source) return;
  if (typeof memoryDraft.source.close === "function") {
    memoryDraft.source.close();
  }
  if (memoryDraft.objectUrl) {
    URL.revokeObjectURL(memoryDraft.objectUrl);
  }
  memoryDraft = null;
}

function clampMemoryOffsets() {
  if (!memoryDraft) return;
  const minX = Math.min(0, MEMORY_WIDTH - memoryDraft.drawWidth);
  const minY = Math.min(0, MEMORY_HEIGHT - memoryDraft.drawHeight);
  memoryDraft.offsetX = Math.min(0, Math.max(minX, memoryDraft.offsetX));
  memoryDraft.offsetY = Math.min(0, Math.max(minY, memoryDraft.offsetY));
}

function renderMemoryDraft() {
  if (!memoryDraft) {
    drawMemoryPlaceholder();
    return;
  }

  const ctx = memoryPreview.getContext("2d", { willReadFrequently: true });
  clampMemoryOffsets();
  ctx.clearRect(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);
  ctx.fillStyle = "#02070f";
  ctx.fillRect(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";
  ctx.drawImage(
    memoryDraft.source,
    memoryDraft.offsetX,
    memoryDraft.offsetY,
    memoryDraft.drawWidth,
    memoryDraft.drawHeight
  );

  ctx.strokeStyle = "rgba(255,255,255,0.14)";
  ctx.strokeRect(0.5, 0.5, MEMORY_WIDTH - 1, MEMORY_HEIGHT - 1);

  memoryRgb565Buffer = canvasToRgb565Bytes(memoryPreview);
  memoryDraftDirty = true;
}

function memoryCanvasPoint(event) {
  const rect = memoryPreview.getBoundingClientRect();
  const scaleX = MEMORY_WIDTH / rect.width;
  const scaleY = MEMORY_HEIGHT / rect.height;
  return {
    x: (event.clientX - rect.left) * scaleX,
    y: (event.clientY - rect.top) * scaleY,
  };
}

function drawMemoryPlaceholder() {
  const ctx = memoryPreview.getContext("2d");
  ctx.clearRect(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);
  const gradient = ctx.createLinearGradient(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);
  gradient.addColorStop(0, "#173d63");
  gradient.addColorStop(0.58, "#0b2137");
  gradient.addColorStop(1, "#07131f");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);

  ctx.fillStyle = "rgba(255,255,255,0.04)";
  for (let x = 0; x < MEMORY_WIDTH; x += 16) {
    ctx.fillRect(x, 0, 1, MEMORY_HEIGHT);
  }
  for (let y = 0; y < MEMORY_HEIGHT; y += 16) {
    ctx.fillRect(0, y, MEMORY_WIDTH, 1);
  }

  ctx.strokeStyle = "rgba(117, 193, 244, 0.38)";
  ctx.strokeRect(0.5, 0.5, MEMORY_WIDTH - 1, MEMORY_HEIGHT - 1);

  ctx.fillStyle = "rgba(9, 23, 39, 0.86)";
  ctx.strokeStyle = "rgba(108, 192, 242, 0.26)";
  ctx.lineWidth = 1;
  ctx.fillRect(18, 18, 124, 92);
  ctx.strokeRect(18.5, 18.5, 123, 91);

  ctx.strokeStyle = "rgba(99, 209, 255, 0.78)";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.roundRect(53, 31, 54, 40, 10);
  ctx.stroke();

  ctx.beginPath();
  ctx.arc(91, 41, 4, 0, Math.PI * 2);
  ctx.fillStyle = "rgba(89, 243, 190, 0.95)";
  ctx.fill();

  ctx.beginPath();
  ctx.moveTo(62, 62);
  ctx.lineTo(74, 47);
  ctx.lineTo(84, 57);
  ctx.lineTo(97, 44);
  ctx.stroke();

  ctx.fillStyle = "#f2f8ff";
  ctx.font = "600 11px Manrope, sans-serif";
  ctx.textAlign = "center";
  ctx.fillText("Memory Photo", 80, 85);

  ctx.fillStyle = "#9dbad3";
  ctx.font = "10px Manrope, sans-serif";
  ctx.fillText("Choose a keepsake and frame it", 80, 98);

  ctx.fillStyle = "rgba(99, 209, 255, 0.16)";
  ctx.fillRect(40, 106, 80, 6);
  ctx.fillStyle = "rgba(89, 243, 190, 0.9)";
  ctx.fillRect(40, 106, 32, 6);
  ctx.textAlign = "start";
}

function canvasToRgb565Bytes(canvas) {
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  const { data } = ctx.getImageData(0, 0, MEMORY_WIDTH, MEMORY_HEIGHT);
  const bytes = new Uint8Array(MEMORY_BYTES);

  for (let i = 0, out = 0; i < data.length; i += 4, out += 2) {
    const r = data[i];
    const g = data[i + 1];
    const b = data[i + 2];
    const value = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    bytes[out] = value & 0xff;
    bytes[out + 1] = (value >> 8) & 0xff;
  }

  return bytes;
}

async function prepareMemoryPhoto(file) {
  if (!file) {
    cleanupMemoryDraftSource();
    memoryRgb565Buffer = null;
    memoryDraftDirty = false;
    drawMemoryPlaceholder();
    setMemorySelection("No image selected.");
    setMemoryStatus("No image prepared yet.");
    return;
  }

  cleanupMemoryDraftSource();
  let sourceWidth = 0;
  let sourceHeight = 0;
  let drawable = null;
  let objectUrl = "";

  if (typeof createImageBitmap === "function") {
    drawable = await createImageBitmap(file);
    sourceWidth = drawable.width;
    sourceHeight = drawable.height;
  } else {
    drawable = await new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => resolve(image);
      image.onerror = () => reject(new Error("Browser could not open the selected image."));
      objectUrl = URL.createObjectURL(file);
      image.src = objectUrl;
    });
    sourceWidth = drawable.naturalWidth || drawable.width;
    sourceHeight = drawable.naturalHeight || drawable.height;
  }

  const scale = Math.max(MEMORY_WIDTH / sourceWidth, MEMORY_HEIGHT / sourceHeight);
  memoryDraft = {
    fileName: file.name,
    source: drawable,
    objectUrl,
    drawWidth: sourceWidth * scale,
    drawHeight: sourceHeight * scale,
    offsetX: (MEMORY_WIDTH - (sourceWidth * scale)) / 2,
    offsetY: (MEMORY_HEIGHT - (sourceHeight * scale)) / 2,
  };

  renderMemoryDraft();
  memoryInfo.textContent = `${MEMORY_WIDTH} x ${MEMORY_HEIGHT} RGB565 · ${MEMORY_BYTES} bytes`;
  setMemorySelection(`Selected: ${file.name}`);
  setMemoryStatus(`Drag the preview to frame "${file.name}" before upload.`, "ok");
}

async function loadMemoryPhotoState() {
  const memory = await fetchJson("/api/memory");
  memoryInfo.textContent = `${memory.width} x ${memory.height} RGB565 · ${memory.bytes} bytes`;

  if (!memoryDraftDirty) {
    setMemoryStatus(
      memory.ready
        ? "Device already has a memory photo."
        : "No memory photo stored on device."
    );
  }
  if (!memoryDraft) {
    setMemorySelection(memory.ready ? "Device memory photo is ready." : "No image selected.");
  }
}

async function uploadMemoryPhoto() {
  if (!memoryRgb565Buffer || memoryRgb565Buffer.byteLength !== MEMORY_BYTES) {
    setMemoryStatus("Choose an image first so the browser can prepare it.", "bad");
    return;
  }

  memoryLoadBtn.disabled = true;
  setMemoryStatus("Uploading memory photo to device...", "warn");

  try {
    const response = await fetch("/api/memory/photo", {
      method: "POST",
      headers: {
        "Content-Type": "application/octet-stream",
      },
      body: memoryRgb565Buffer,
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok || result.ok === false) {
      throw new Error(result.message || `Upload failed: ${response.status}`);
    }

    memoryDraftDirty = false;
    setMemoryStatus(result.message || "Memory photo uploaded.", "ok");
    setStatus("Memory photo is ready on the device.", "ok");
    await loadMemoryPhotoState().catch(() => {});
  } catch (error) {
    setMemoryStatus(error.message, "bad");
  } finally {
    memoryLoadBtn.disabled = false;
  }
}

async function clearMemoryPhoto() {
  memoryClearBtn.disabled = true;
  setMemoryStatus("Clearing memory photo...", "warn");

  try {
    const result = await fetchJson("/api/memory/photo", {
      method: "DELETE",
    });
    cleanupMemoryDraftSource();
    memoryRgb565Buffer = null;
    memoryDraftDirty = false;
    memoryFileInput.value = "";
    drawMemoryPlaceholder();
    setMemorySelection("No image selected.");
    setMemoryStatus(result.message || "Memory photo cleared.", "ok");
    setStatus("Memory photo cleared from device.", "ok");
    await loadMemoryPhotoState().catch(() => {});
  } catch (error) {
    setMemoryStatus(error.message, "bad");
  } finally {
    memoryClearBtn.disabled = false;
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
      hiddenInput.checked = false;
      if (!item.secure) {
        passwordInput.value = "";
      }
      markWifiFormDirty();
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

  ssidInput.addEventListener("input", markWifiFormDirty);
  passwordInput.addEventListener("input", markWifiFormDirty);
  hiddenInput.addEventListener("change", markWifiFormDirty);

  wifiForm.addEventListener("submit", async (event) => {
    event.preventDefault();

    const ssid = ssidInput.value.trim();
    const password = passwordInput.value;
    const hidden = hiddenInput.checked;

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
      if (hidden) {
        body.set("hidden", "1");
      }

      const result = await fetchJson("/api/wifi", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: body.toString(),
      });

      const message = result.message || "Saved. Device restarting.";
      resetWifiFormDirty();
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

  memoryFileInput.addEventListener("change", async (event) => {
    const file = event.target.files?.[0];
    if (!file) {
      await prepareMemoryPhoto(null);
      return;
    }

    try {
      await prepareMemoryPhoto(file);
    } catch (error) {
      memoryRgb565Buffer = null;
      memoryDraftDirty = false;
      drawMemoryPlaceholder();
      setMemorySelection("No image selected.");
      setMemoryStatus(error.message || "Failed to prepare image.", "bad");
    }
  });

  memoryPreview.addEventListener("pointerdown", (event) => {
    if (!memoryDraft) return;
    const point = memoryCanvasPoint(event);
    memoryPointerDrag = {
      pointerId: event.pointerId,
      startX: point.x,
      startY: point.y,
      offsetX: memoryDraft.offsetX,
      offsetY: memoryDraft.offsetY,
    };
    memoryPreview.setPointerCapture(event.pointerId);
  });

  memoryPreview.addEventListener("pointermove", (event) => {
    if (!memoryDraft || !memoryPointerDrag || memoryPointerDrag.pointerId !== event.pointerId) {
      return;
    }
    const point = memoryCanvasPoint(event);
    memoryDraft.offsetX = memoryPointerDrag.offsetX + (point.x - memoryPointerDrag.startX);
    memoryDraft.offsetY = memoryPointerDrag.offsetY + (point.y - memoryPointerDrag.startY);
    renderMemoryDraft();
  });

  const stopMemoryDrag = (event) => {
    if (!memoryPointerDrag || memoryPointerDrag.pointerId !== event.pointerId) {
      return;
    }
    memoryPreview.releasePointerCapture(event.pointerId);
    memoryPointerDrag = null;
  };

  memoryPreview.addEventListener("pointerup", stopMemoryDrag);
  memoryPreview.addEventListener("pointercancel", stopMemoryDrag);

  memoryLoadBtn.addEventListener("click", uploadMemoryPhoto);
  memoryClearBtn.addEventListener("click", clearMemoryPhoto);

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
  drawMemoryPlaceholder();

  try {
    await Promise.all([loadState(), loadTelemetry(), loadMemoryPhotoState()]);
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
