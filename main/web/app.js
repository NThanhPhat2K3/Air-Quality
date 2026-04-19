const modeBadge = document.getElementById("modeBadge");
const linkBadge = document.getElementById("linkBadge");
const currentSsid = document.getElementById("currentSsid");
const apInfo = document.getElementById("apInfo");
const credSource = document.getElementById("credSource");
const runtimeIp = document.getElementById("runtimeIp");
const quickSsid = document.getElementById("quickSsid");
const quickAccessIp = document.getElementById("quickAccessIp");
const quickAccessLink = document.getElementById("quickAccessLink");
const qrAccessCard = document.getElementById("qrAccessCard");
const qrCanvas = document.getElementById("qrCanvas");
const qrTitle = document.getElementById("qrTitle");
const qrModeBadge = document.getElementById("qrModeBadge");
const qrCaption = document.getElementById("qrCaption");

const metricAqi = document.getElementById("metricAqi");
const metricAqiCard = document.getElementById("metricAqiCard");
const metricAqiState = document.getElementById("metricAqiState");
const metricEco2 = document.getElementById("metricEco2");
const metricTvoc = document.getElementById("metricTvoc");
const metricTemp = document.getElementById("metricTemp");
const metricHumidity = document.getElementById("metricHumidity");

const scanList = document.getElementById("scanList");
const savedWifiList = document.getElementById("savedWifiList");
const savedWifiStatus = document.getElementById("savedWifiStatus");
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
const langButtons = [...document.querySelectorAll(".lang-btn")];

const disconnectBtn = document.getElementById("disconnectBtn");
const forgetBtn = document.getElementById("forgetBtn");
const stopPortalBtn = document.getElementById("stopPortalBtn");

const alarmTime = document.getElementById("alarmTime");
const alarmMessage = document.getElementById("alarmMessage");
const alarmSaveBtn = document.getElementById("alarmSaveBtn");
const alarmClearBtn = document.getElementById("alarmClearBtn");
const alarmInfo = document.getElementById("alarmInfo");

const ALARM_STORAGE_KEY = "aqnode_alarm";
const LANGUAGE_STORAGE_KEY = "aqnode_lang";
const MEMORY_WIDTH = 160;
const MEMORY_HEIGHT = 128;
const MEMORY_BYTES = MEMORY_WIDTH * MEMORY_HEIGHT * 2;
const QR_VERSION = 3;
const QR_SIZE = 29;
const QR_DATA_CODEWORDS = 55;
const QR_EC_CODEWORDS = 15;
const QR_ALIGNMENT_CENTER = 22;

let scanInProgress = false;
let alarmTimer = null;
let wifiFormDirty = false;
let memoryRgb565Buffer = null;
let memoryDraftDirty = false;
let memoryDraft = null;
let memoryPointerDrag = null;
let currentLang = localStorage.getItem(LANGUAGE_STORAGE_KEY) === "vi" ? "vi" : "en";
let lastState = null;

const TEXT = {
  en: {
    "brand.eyebrow": "Air Quality Node",
    "brand.title": "Control Center",
    "tab.monitoring": "Monitoring",
    "tab.wifi": "Wi-Fi Config",
    "tab.memory": "Memory",
    "tab.alarm": "Alarm",
    "meta.mode": "Mode",
    "meta.network": "Network",
    "meta.access": "Access",
    "monitoring.title": "Live Monitoring",
    "monitoring.hint": "Realtime values from the running device.",
    "monitoring.note": "Primary comfort signal for the room right now.",
    "wifi.title": "Connect Device to Wi-Fi",
    "wifi.hint": "Use the setup hotspot, choose a Wi-Fi network, then let the device join it.",
    "wifi.step1.title": "Open setup",
    "wifi.step1.body": "Connect to the device hotspot or tap the setup button below.",
    "wifi.step2.title": "Choose Wi-Fi",
    "wifi.step2.body": "Pick your home or office Wi-Fi, then enter the password if needed.",
    "wifi.step3.title": "Finish",
    "wifi.step3.body": "The device will connect and return to normal mode automatically.",
    "wifi.shortcut.kicker": "Setup Shortcut",
    "wifi.shortcut.title": "Scan this to open the device setup page",
    "wifi.shortcut.body": "If setup mode is active, your phone can scan this QR and open the right page instantly.",
    "wifi.shortcut.hotspot": "Device hotspot",
    "wifi.shortcut.address": "Open address",
    "wifi.shortcut.link": "Direct link",
    "wifi.qr.kicker": "Phone Access",
    "wifi.meta.current": "Current Wi-Fi",
    "wifi.meta.hotspot": "Setup hotspot",
    "wifi.meta.saved": "Saved settings",
    "wifi.saved.title": "Previously Used Wi-Fi",
    "wifi.saved.hint": "Tap one saved network to reconnect without typing the password again.",
    "wifi.scan.title": "Choose a Wi-Fi network",
    "wifi.scan.portal": "Open Setup Hotspot",
    "wifi.scan.find": "Find Wi-Fi",
    "wifi.scan.hint": "Tap a network below to fill in the name automatically.",
    "wifi.form.title": "Enter Wi-Fi details",
    "wifi.form.name": "Wi-Fi name",
    "wifi.form.password": "Password",
    "wifi.form.hidden": "This Wi-Fi is hidden",
    "wifi.form.hint": "Leave the password empty only if this Wi-Fi does not require one.",
    "wifi.form.submit": "Connect Device",
    "wifi.actions.title": "Advanced Actions",
    "wifi.actions.disconnect": "Disconnect and Open Setup",
    "wifi.actions.forget": "Erase Saved Wi-Fi",
    "wifi.actions.stop": "Close Setup Hotspot",
    "alarm.title": "Alarm Scheduler",
    "alarm.hint": "Set quick reminders in the web console (local browser storage).",
    "alarm.time": "Alarm Time",
    "alarm.message": "Message",
    "alarm.placeholder": "Check sensor and filters",
    "alarm.save": "Save Alarm",
    "alarm.clear": "Clear",
    "badge.loading": "Loading",
    "badge.checking": "Checking",
    "badge.provisioning": "Provisioning",
    "badge.runtime": "Runtime",
    "badge.connected": "Wi-Fi Connected",
    "badge.offline": "Offline",
    "badge.setupReady": "Setup Ready",
    "badge.directLink": "Direct Link",
    "label.none": "(none)",
    "label.notSet": "Not set yet",
    "label.savedOnDevice": "Saved on this device",
    "label.firmwareDefault": "Using default firmware setting",
    "label.password": "Password",
    "label.provisioningQr": "Provisioning QR",
    "label.runtimeQr": "Runtime QR",
    "label.waiting": "Waiting",
    "qr.empty": "No scannable local address yet. Start provisioning or wait for Wi-Fi IP.",
    "qr.aria": "QR code to open local Air Quality portal",
    "qr.caption.provisioning": "Connect your phone to the device hotspot, then scan this QR to open setup.",
    "qr.caption.runtime": "If your phone is on the same network, this QR opens the device page directly.",
    "aqi.waiting": "Waiting",
    "aqi.l1": "Level 1 · Excellent",
    "aqi.l2": "Level 2 · Good",
    "aqi.l3": "Level 3 · Moderate",
    "aqi.l4": "Level 4 · Poor",
    "aqi.l5": "Level 5 · Unhealthy",
    "status.uiMismatch": "UI mismatch ({items}). Please hard refresh page (Ctrl+F5).",
    "status.scanEmpty": "No Wi-Fi networks were found nearby. Try again from a closer spot.",
    "status.networkSecure": "Password required · Signal {rssi} dBm",
    "status.networkOpen": "No password needed · Signal {rssi} dBm",
    "status.secure": "Secure",
    "status.open": "Open",
    "status.selectedWifi": "Selected Wi-Fi: {ssid}",
    "status.savedWifiEmpty": "Once the device connects successfully, the most recent Wi-Fi networks will appear here.",
    "status.savedWifiUse": "Use Again",
    "status.savedWifiCurrent": "Current choice",
    "status.savedWifiHidden": "Hidden network",
    "status.savedWifiReady": "Saved on device",
    "status.savedWifiConnecting": "Trying saved Wi-Fi: {ssid}...",
    "status.savedWifiSelected": "Trying saved Wi-Fi now.",
    "status.scanning": "Looking for nearby Wi-Fi networks...",
    "status.scanFound": "Found {count} Wi-Fi networks.",
    "status.enterWifiName": "Please enter the Wi-Fi name.",
    "status.testingWifi": "Trying this Wi-Fi on the device. This can take a few seconds...",
    "status.connectedSaved": "Connected! Credentials saved.",
    "status.requestTimedOut": "Request timed out. The device may still be testing.",
    "status.enablePortal": "Enabling setup hotspot...",
    "status.portalActive": "Setup hotspot is active.",
    "status.disconnectingWifi": "Disconnecting Wi-Fi...",
    "status.wifiDisconnected": "Wi-Fi disconnected.",
    "status.eraseConfirm": "Erase saved Wi-Fi credentials from device? You will need to re-enter them.",
    "status.erasingCredentials": "Erasing credentials...",
    "status.credentialsErased": "Credentials erased.",
    "status.stoppingPortal": "Stopping provisioning portal...",
    "status.provisioningStopped": "Provisioning stopped.",
    "status.noAlarm": "No alarm configured.",
    "status.noImageSelected": "No image selected.",
    "status.alarmSet": "Alarm set at {time} - {message}",
    "status.noMessage": "No message",
    "status.alarmAlert": "AQ Node Alarm: {message}",
    "status.checkDevice": "Time to check your device.",
    "status.setAlarmFirst": "Please set alarm time first.",
    "status.alarmSaved": "Alarm saved on this browser.",
    "status.alarmCleared": "Alarm cleared.",
    "status.dashboardReady": "Dashboard ready.",
    "status.bootFailed": "Boot failed: {message}",
  },
  vi: {
    "brand.eyebrow": "Thiet bi khong khi",
    "brand.title": "Trung tâm điều khiển",
    "tab.monitoring": "Theo dõi",
    "tab.wifi": "Cài Wi-Fi",
    "tab.memory": "Kỷ niệm",
    "tab.alarm": "Báo thức",
    "meta.mode": "Chế độ",
    "meta.network": "Kết nối",
    "meta.access": "Truy cập",
    "monitoring.title": "Giám sát trực tiếp",
    "monitoring.hint": "Các giá trị đang chạy theo thời gian thực từ thiết bị.",
    "monitoring.note": "Chỉ số dễ nhìn nhất để biết chất lượng không khí hiện tại.",
    "wifi.title": "Kết nối thiết bị vào Wi-Fi",
    "wifi.hint": "Mở hotspot cài đặt, chọn mạng Wi‑Fi, rồi để thiết bị tự kết nối.",
    "wifi.step1.title": "Mở cài đặt",
    "wifi.step1.body": "Kết nối vào hotspot của thiết bị hoặc bấm nút mở cài đặt bên dưới.",
    "wifi.step2.title": "Chọn Wi‑Fi",
    "wifi.step2.body": "Chọn Wi‑Fi ở nhà hoặc văn phòng, rồi nhập mật khẩu nếu cần.",
    "wifi.step3.title": "Hoàn tất",
    "wifi.step3.body": "Thiết bị sẽ tự kết nối và quay lại chế độ bình thường.",
    "wifi.shortcut.kicker": "Lối vào nhanh",
    "wifi.shortcut.title": "Quét mã để mở trang cài đặt thiết bị",
    "wifi.shortcut.body": "Nếu chế độ cài đặt đang bật, điện thoại có thể quét QR này để vào đúng trang ngay.",
    "wifi.shortcut.hotspot": "Hotspot thiết bị",
    "wifi.shortcut.address": "Địa chỉ mở",
    "wifi.shortcut.link": "Liên kết trực tiếp",
    "wifi.qr.kicker": "Mở bằng điện thoại",
    "wifi.meta.current": "Wi‑Fi hiện tại",
    "wifi.meta.hotspot": "Hotspot cài đặt",
    "wifi.meta.saved": "Thiết lập đã lưu",
    "wifi.saved.title": "Wi‑Fi đã từng dùng",
    "wifi.saved.hint": "Chạm một mạng đã lưu để kết nối lại mà không cần nhập mật khẩu nữa.",
    "wifi.scan.title": "Chọn một mạng Wi‑Fi",
    "wifi.scan.portal": "Mở Hotspot Cài Đặt",
    "wifi.scan.find": "Tìm Wi‑Fi",
    "wifi.scan.hint": "Chạm vào một mạng bên dưới để tự điền tên.",
    "wifi.form.title": "Nhập thông tin Wi‑Fi",
    "wifi.form.name": "Tên Wi‑Fi",
    "wifi.form.password": "Mật khẩu",
    "wifi.form.hidden": "Wi‑Fi này đang ẩn",
    "wifi.form.hint": "Chỉ để trống mật khẩu nếu Wi‑Fi này không cần mật khẩu.",
    "wifi.form.submit": "Kết nối thiết bị",
    "wifi.actions.title": "Tác vụ nâng cao",
    "wifi.actions.disconnect": "Ngắt Wi‑Fi và mở cài đặt",
    "wifi.actions.forget": "Xóa Wi‑Fi đã lưu",
    "wifi.actions.stop": "Đóng hotspot cài đặt",
    "alarm.title": "Hẹn giờ báo thức",
    "alarm.hint": "Tạo nhắc nhở nhanh trên web này (lưu trong trình duyệt).",
    "alarm.time": "Giờ báo thức",
    "alarm.message": "Lời nhắc",
    "alarm.placeholder": "Kiểm tra cảm biến và bộ lọc",
    "alarm.save": "Lưu báo thức",
    "alarm.clear": "Xóa",
    "badge.loading": "Đang tải",
    "badge.checking": "Đang kiểm tra",
    "badge.provisioning": "Cài đặt",
    "badge.runtime": "Hoạt động",
    "badge.connected": "Đã nối Wi‑Fi",
    "badge.offline": "Ngoại tuyến",
    "badge.setupReady": "Sẵn sàng cài đặt",
    "badge.directLink": "Mở trực tiếp",
    "label.none": "(chưa có)",
    "label.notSet": "Chưa có",
    "label.savedOnDevice": "Đã lưu trên thiết bị",
    "label.firmwareDefault": "Đang dùng thiết lập mặc định của firmware",
    "label.password": "Mật khẩu",
    "label.provisioningQr": "QR cài đặt",
    "label.runtimeQr": "QR truy cập",
    "label.waiting": "Đang chờ",
    "qr.empty": "Chưa có địa chỉ để quét. Hãy bật chế độ cài đặt hoặc chờ thiết bị nhận IP Wi‑Fi.",
    "qr.aria": "Mã QR để mở trang cục bộ của Air Quality",
    "qr.caption.provisioning": "Kết nối điện thoại vào hotspot của thiết bị rồi quét QR này để mở phần cài đặt.",
    "qr.caption.runtime": "Nếu điện thoại cùng mạng với thiết bị, QR này sẽ mở trang thiết bị trực tiếp.",
    "aqi.waiting": "Đang chờ",
    "aqi.l1": "Mức 1 · Rất tốt",
    "aqi.l2": "Mức 2 · Tốt",
    "aqi.l3": "Mức 3 · Trung bình",
    "aqi.l4": "Mức 4 · Kém",
    "aqi.l5": "Mức 5 · Không tốt",
    "status.uiMismatch": "UI chưa khớp ({items}). Hãy hard refresh trang (Ctrl+F5).",
    "status.scanEmpty": "Không tìm thấy mạng Wi‑Fi nào gần đây. Hãy thử lại khi ở gần thiết bị hơn.",
    "status.networkSecure": "Cần mật khẩu · Tín hiệu {rssi} dBm",
    "status.networkOpen": "Không cần mật khẩu · Tín hiệu {rssi} dBm",
    "status.secure": "Có khóa",
    "status.open": "Mở",
    "status.selectedWifi": "Đã chọn Wi‑Fi: {ssid}",
    "status.savedWifiEmpty": "Sau khi thiết bị kết nối thành công, các mạng gần đây sẽ hiện ở đây để chọn lại nhanh.",
    "status.savedWifiUse": "Dùng lại",
    "status.savedWifiCurrent": "Đang dùng",
    "status.savedWifiHidden": "Mạng ẩn",
    "status.savedWifiReady": "Đã lưu trên thiết bị",
    "status.savedWifiConnecting": "Đang thử Wi‑Fi đã lưu: {ssid}...",
    "status.savedWifiSelected": "Đang thử Wi‑Fi đã lưu.",
    "status.scanning": "Đang tìm các mạng Wi‑Fi gần đây...",
    "status.scanFound": "Đã tìm thấy {count} mạng Wi‑Fi.",
    "status.enterWifiName": "Vui lòng nhập tên Wi‑Fi.",
    "status.testingWifi": "Thiết bị đang thử kết nối Wi‑Fi này. Quá trình này có thể mất vài giây...",
    "status.connectedSaved": "Đã kết nối! Thông tin đã được lưu.",
    "status.requestTimedOut": "Yêu cầu đã hết thời gian chờ. Thiết bị có thể vẫn đang kiểm tra.",
    "status.enablePortal": "Đang bật hotspot cài đặt...",
    "status.portalActive": "Hotspot cài đặt đang hoạt động.",
    "status.disconnectingWifi": "Đang ngắt Wi‑Fi...",
    "status.wifiDisconnected": "Đã ngắt Wi‑Fi.",
    "status.eraseConfirm": "Xóa Wi‑Fi đã lưu khỏi thiết bị nhé? Sau đó sẽ cần nhập lại.",
    "status.erasingCredentials": "Đang xóa thông tin đã lưu...",
    "status.credentialsErased": "Đã xóa thông tin Wi‑Fi.",
    "status.stoppingPortal": "Đang tắt cổng cài đặt...",
    "status.provisioningStopped": "Đã tắt chế độ cài đặt.",
    "status.noAlarm": "Chưa có báo thức nào.",
    "status.noImageSelected": "Chưa chọn ảnh nào.",
    "status.alarmSet": "Báo thức lúc {time} - {message}",
    "status.noMessage": "Không có lời nhắc",
    "status.alarmAlert": "Báo thức AQ Node: {message}",
    "status.checkDevice": "Đến giờ kiểm tra thiết bị rồi.",
    "status.setAlarmFirst": "Vui lòng chọn giờ báo thức trước.",
    "status.alarmSaved": "Đã lưu báo thức trên trình duyệt này.",
    "status.alarmCleared": "Đã xóa báo thức.",
    "status.dashboardReady": "Bảng điều khiển đã sẵn sàng.",
    "status.bootFailed": "Khởi động thất bại: {message}",
  },
};

function t(key, vars = {}) {
  const template = TEXT[currentLang]?.[key] ?? TEXT.en[key] ?? key;
  return template.replace(/\{(\w+)\}/g, (_, name) => `${vars[name] ?? ""}`);
}

function setLanguage(lang) {
  currentLang = lang === "vi" ? "vi" : "en";
  localStorage.setItem(LANGUAGE_STORAGE_KEY, currentLang);
  document.documentElement.lang = currentLang;
  langButtons.forEach((button) => {
    button.classList.toggle("active", button.dataset.lang === currentLang);
  });
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
  document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
    el.setAttribute("placeholder", t(el.dataset.i18nPlaceholder));
  });
  if (!lastState) {
    setBadge(modeBadge, t("badge.loading"));
    setBadge(linkBadge, t("badge.checking"), "muted");
    metricAqiState.textContent = t("label.waiting");
    qrModeBadge.textContent = t("label.waiting");
  }
  renderAlarm();
  if (lastState) {
    renderState(lastState);
  }
}

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

function setSavedWifiStatus(message, type = "") {
  if (!savedWifiStatus) {
    return;
  }
  savedWifiStatus.textContent = message || "";
  savedWifiStatus.className = `hint saved-wifi-status ${type}`.trim();
}

function setBadge(el, text, variant = "") {
  if (!el) return;
  el.textContent = text;
  el.className = `badge ${variant}`.trim();
}

function formatSource(source) {
  if (source === "nvs") return t("label.savedOnDevice");
  if (source === "build") return t("label.firmwareDefault");
  return t("label.notSet");
}

function initQrMath() {
  const exp = new Array(512).fill(0);
  const log = new Array(256).fill(0);
  let value = 1;

  for (let i = 0; i < 255; i += 1) {
    exp[i] = value;
    log[value] = i;
    value <<= 1;
    if (value & 0x100) {
      value ^= 0x11d;
    }
  }

  for (let i = 255; i < 512; i += 1) {
    exp[i] = exp[i - 255];
  }

  return { exp, log };
}

const QR_GF = initQrMath();

function gfMul(a, b) {
  if (a === 0 || b === 0) {
    return 0;
  }
  return QR_GF.exp[QR_GF.log[a] + QR_GF.log[b]];
}

function buildGeneratorPoly(degree) {
  let poly = [1];

  for (let i = 0; i < degree; i += 1) {
    const next = new Array(poly.length + 1).fill(0);
    for (let j = 0; j < poly.length; j += 1) {
      next[j] ^= poly[j];
      next[j + 1] ^= gfMul(poly[j], QR_GF.exp[i]);
    }
    poly = next;
  }

  return poly;
}

const QR_GENERATOR = buildGeneratorPoly(QR_EC_CODEWORDS);

function buildQrCodewords(text) {
  const bytes = [...new TextEncoder().encode(text)];
  if (bytes.length > 53) {
    return null;
  }

  const bits = [];
  const pushBits = (value, width) => {
    for (let bit = width - 1; bit >= 0; bit -= 1) {
      bits.push((value >> bit) & 1);
    }
  };

  pushBits(0b0100, 4);
  pushBits(bytes.length, 8);
  bytes.forEach((byte) => pushBits(byte, 8));

  const maxBits = QR_DATA_CODEWORDS * 8;
  const terminatorBits = Math.min(4, maxBits - bits.length);
  for (let i = 0; i < terminatorBits; i += 1) {
    bits.push(0);
  }
  while (bits.length % 8 !== 0) {
    bits.push(0);
  }

  const data = [];
  for (let i = 0; i < bits.length; i += 8) {
    let byte = 0;
    for (let bit = 0; bit < 8; bit += 1) {
      byte = (byte << 1) | bits[i + bit];
    }
    data.push(byte);
  }

  const pads = [0xec, 0x11];
  while (data.length < QR_DATA_CODEWORDS) {
    data.push(pads[data.length % 2]);
  }

  const ec = new Array(QR_EC_CODEWORDS).fill(0);
  data.forEach((byte) => {
    const factor = byte ^ ec[0];
    ec.shift();
    ec.push(0);
    if (factor === 0) {
      return;
    }
    for (let i = 0; i < QR_EC_CODEWORDS; i += 1) {
      ec[i] ^= gfMul(QR_GENERATOR[i + 1], factor);
    }
  });

  return [...data, ...ec];
}

function createQrMatrix(text) {
  const codewords = buildQrCodewords(text);
  if (!codewords) {
    return null;
  }

  const size = QR_SIZE;
  const modules = Array.from({ length: size }, () => Array(size).fill(false));
  const assigned = Array.from({ length: size }, () => Array(size).fill(false));
  const setModule = (x, y, value) => {
    modules[y][x] = Boolean(value);
    assigned[y][x] = true;
  };
  const reserveModule = (x, y, value = false) => {
    if (!assigned[y][x]) {
      modules[y][x] = Boolean(value);
      assigned[y][x] = true;
    }
  };
  const drawFinder = (left, top) => {
    for (let y = -1; y <= 7; y += 1) {
      for (let x = -1; x <= 7; x += 1) {
        const xx = left + x;
        const yy = top + y;
        if (xx < 0 || yy < 0 || xx >= size || yy >= size) {
          continue;
        }
        const outer = x >= 0 && x <= 6 && y >= 0 && y <= 6;
        const border = x === 0 || x === 6 || y === 0 || y === 6;
        const inner = x >= 2 && x <= 4 && y >= 2 && y <= 4;
        setModule(xx, yy, outer && (border || inner));
      }
    }
  };
  const drawAlignment = (centerX, centerY) => {
    for (let y = -2; y <= 2; y += 1) {
      for (let x = -2; x <= 2; x += 1) {
        const xx = centerX + x;
        const yy = centerY + y;
        const distance = Math.max(Math.abs(x), Math.abs(y));
        setModule(xx, yy, distance !== 1);
      }
    }
  };

  drawFinder(0, 0);
  drawFinder(size - 7, 0);
  drawFinder(0, size - 7);
  drawAlignment(QR_ALIGNMENT_CENTER, QR_ALIGNMENT_CENTER);

  for (let i = 8; i < size - 8; i += 1) {
    setModule(i, 6, i % 2 === 0);
    setModule(6, i, i % 2 === 0);
  }
  setModule(8, (4 * QR_VERSION) + 9, true);

  for (let i = 0; i < 9; i += 1) {
    if (i !== 6) {
      reserveModule(8, i);
      reserveModule(i, 8);
    }
  }
  for (let i = 0; i < 8; i += 1) {
    reserveModule(size - 1 - i, 8);
    reserveModule(8, size - 1 - i);
  }

  const bitStream = [];
  codewords.forEach((codeword) => {
    for (let bit = 7; bit >= 0; bit -= 1) {
      bitStream.push((codeword >> bit) & 1);
    }
  });

  let bitIndex = 0;
  let direction = -1;
  for (let x = size - 1; x > 0; x -= 2) {
    if (x === 6) {
      x -= 1;
    }
    for (let step = 0; step < size; step += 1) {
      const y = direction === -1 ? size - 1 - step : step;
      for (let dx = 0; dx < 2; dx += 1) {
        const xx = x - dx;
        if (assigned[y][xx]) {
          continue;
        }
        let value = bitStream[bitIndex] === 1;
        bitIndex += 1;
        if ((xx + y) % 2 === 0) {
          value = !value;
        }
        modules[y][xx] = value;
        assigned[y][xx] = true;
      }
    }
    direction *= -1;
  }

  const formatData = (0b01 << 3) | 0;
  let remainder = formatData << 10;
  while (remainder >= (1 << 10)) {
    const shift = Math.floor(Math.log2(remainder)) - 10;
    remainder ^= 0x537 << shift;
  }
  const format = ((formatData << 10) | remainder) ^ 0x5412;
  for (let i = 0; i <= 5; i += 1) {
    modules[i][8] = ((format >> i) & 1) === 1;
  }
  modules[7][8] = ((format >> 6) & 1) === 1;
  modules[8][8] = ((format >> 7) & 1) === 1;
  modules[8][7] = ((format >> 8) & 1) === 1;
  for (let i = 9; i < 15; i += 1) {
    modules[8][14 - i] = ((format >> i) & 1) === 1;
  }

  for (let i = 0; i < 8; i += 1) {
    modules[8][size - 1 - i] = ((format >> i) & 1) === 1;
  }
  for (let i = 8; i < 15; i += 1) {
    modules[size - 15 + i][8] = ((format >> i) & 1) === 1;
  }

  return modules;
}

function renderQrCode(target, text) {
  if (!target) {
    return false;
  }

  const matrix = text ? createQrMatrix(text) : null;
  if (!matrix) {
    target.innerHTML = `<div class="wifi-qr-empty">${t("qr.empty")}</div>`;
    return false;
  }

  const quietZone = 4;
  const viewSize = matrix.length + (quietZone * 2);
  const rects = [];
  for (let y = 0; y < matrix.length; y += 1) {
    for (let x = 0; x < matrix.length; x += 1) {
      if (matrix[y][x]) {
        rects.push(`<rect x="${x + quietZone}" y="${y + quietZone}" width="1" height="1"/>`);
      }
    }
  }

  target.innerHTML = `<svg viewBox="0 0 ${viewSize} ${viewSize}" role="img" aria-label="${t("qr.aria")}" xmlns="http://www.w3.org/2000/svg"><rect width="${viewSize}" height="${viewSize}" fill="#ffffff"/><g fill="#061321">${rects.join("")}</g></svg>`;
  return true;
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
    [quickSsid, "quickSsid"],
    [quickAccessIp, "quickAccessIp"],
    [quickAccessLink, "quickAccessLink"],
    [qrAccessCard, "qrAccessCard"],
    [qrCanvas, "qrCanvas"],
    [qrTitle, "qrTitle"],
    [qrModeBadge, "qrModeBadge"],
    [qrCaption, "qrCaption"],
    [metricAqi, "metricAqi"],
    [metricAqiCard, "metricAqiCard"],
    [metricAqiState, "metricAqiState"],
    [metricEco2, "metricEco2"],
    [metricTvoc, "metricTvoc"],
    [metricTemp, "metricTemp"],
    [metricHumidity, "metricHumidity"],
    [savedWifiList, "savedWifiList"],
    [savedWifiStatus, "savedWifiStatus"],
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
    [disconnectBtn, "disconnectBtn"],
    [forgetBtn, "forgetBtn"],
    [stopPortalBtn, "stopPortalBtn"],
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
    setStatus(t("status.uiMismatch", { items: missing.join(", ") }), "bad");
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
    return { label: t("aqi.waiting"), tone: "idle" };
  }

  const level = Number(aqi);
  if (level <= 1) return { label: t("aqi.l1"), tone: "good" };
  if (level === 2) return { label: t("aqi.l2"), tone: "good" };
  if (level === 3) return { label: t("aqi.l3"), tone: "mid" };
  if (level === 4) return { label: t("aqi.l4"), tone: "warn" };
  return { label: t("aqi.l5"), tone: "bad" };
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
  lastState = state;
  renderState(state);
}

function renderState(state) {
  setBadge(modeBadge, state.mode === "provisioning" ? t("badge.provisioning") : t("badge.runtime"));
  setBadge(linkBadge, state.connected ? t("badge.connected") : t("badge.offline"), state.connected ? "ok" : "warn");

  currentSsid.textContent = state.currentSsid || t("label.none");
  apInfo.textContent = state.apSsid
    ? `${state.apSsid} · ${t("label.password")}: ${state.apPassword || "-"}`
    : "-";
  credSource.textContent = formatSource(state.credentialSource);
  quickSsid.textContent = state.provisioning ? (state.apSsid || "-") : (state.currentSsid || t("label.none"));
  quickAccessIp.textContent = state.accessIp || state.runtimeIp || state.apIp || "-";

  if (state.accessUrl) {
    runtimeIp.textContent = state.accessUrl;
  } else if (state.runtimeHost) {
    runtimeIp.textContent = `http://${state.runtimeHost}`;
  } else if (state.runtimeIp) {
    runtimeIp.textContent = `http://${state.runtimeIp}`;
  } else {
    runtimeIp.textContent = "-";
  }

  const accessUrl = state.accessUrl || "";
  quickAccessLink.textContent = accessUrl || "-";
  if (accessUrl) {
    quickAccessLink.href = accessUrl;
  } else {
    quickAccessLink.removeAttribute("href");
  }
  quickAccessLink.tabIndex = accessUrl ? 0 : -1;
  qrTitle.textContent = state.provisioning ? t("label.provisioningQr") : t("label.runtimeQr");
  setBadge(qrModeBadge, state.provisioning ? t("badge.setupReady") : t("badge.directLink"), state.provisioning ? "ok" : "muted");
  qrCaption.textContent = state.provisioning ? t("qr.caption.provisioning") : t("qr.caption.runtime");
  renderQrCode(qrCanvas, accessUrl);
  renderSavedNetworks(state.savedNetworks || []);

  enablePortalBtn.disabled = !state.canStartPortal;

  if (!ssidInput.value && state.currentSsid) {
    ssidInput.value = state.currentSsid;
  }
  if (!wifiFormDirty) {
    hiddenInput.checked = Boolean(state.hiddenSsid);
  }
}

function renderSavedNetworks(items) {
  savedWifiList.innerHTML = "";

  if (!items || items.length === 0) {
    setSavedWifiStatus("");
    const empty = document.createElement("p");
    empty.className = "hint saved-wifi-empty";
    empty.textContent = t("status.savedWifiEmpty");
    savedWifiList.appendChild(empty);
    return;
  }

  items.forEach((item) => {
    const row = document.createElement("article");
    row.className = "saved-wifi-item";

    const main = document.createElement("div");
    main.className = "saved-wifi-main";

    const title = document.createElement("div");
    title.className = "saved-wifi-title";

    const ssid = document.createElement("span");
    ssid.className = "saved-wifi-ssid";
    ssid.textContent = item.ssid || t("label.none");
    title.appendChild(ssid);

    if (item.active) {
      const activeChip = document.createElement("span");
      activeChip.className = "saved-wifi-chip active";
      activeChip.textContent = t("status.savedWifiCurrent");
      title.appendChild(activeChip);
    }

    const meta = document.createElement("div");
    meta.className = "saved-wifi-meta";

    const readyChip = document.createElement("span");
    readyChip.className = "saved-wifi-chip";
    readyChip.textContent = t("status.savedWifiReady");
    meta.appendChild(readyChip);

    if (item.hidden) {
      const hiddenChip = document.createElement("span");
      hiddenChip.className = "saved-wifi-chip";
      hiddenChip.textContent = t("status.savedWifiHidden");
      meta.appendChild(hiddenChip);
    }

    main.appendChild(title);
    main.appendChild(meta);

    const action = document.createElement("button");
    action.type = "button";
    action.className = "btn ghost saved-wifi-action";
    action.textContent = t("status.savedWifiUse");
    action.disabled = Boolean(item.active);
    action.addEventListener("click", async () => {
      action.disabled = true;
      setSavedWifiStatus(
        item.ssid
          ? t("status.savedWifiConnecting", { ssid: item.ssid })
          : t("status.savedWifiSelected"),
        "warn"
      );
      setStatus(
        item.ssid
          ? t("status.savedWifiConnecting", { ssid: item.ssid })
          : t("status.savedWifiSelected"),
        "warn"
      );

      try {
        const body = new URLSearchParams();
        body.set("slot", String(item.slot));
        const result = await fetchJson("/api/wifi/history/use", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded",
          },
          body: body.toString(),
        });
        setStatus(result.message || t("status.connectedSaved"), "ok");
        setSavedWifiStatus(result.message || t("status.connectedSaved"), "ok");
      } catch (error) {
        setStatus(error.message, "bad");
        setSavedWifiStatus(error.message, "bad");
      } finally {
        await loadState().catch(() => {});
      }
    });

    row.appendChild(main);
    row.appendChild(action);
    savedWifiList.appendChild(row);
  });
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
    metricAqiState.textContent = t("label.waiting");
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
    empty.textContent = t("status.scanEmpty");
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
    meta.textContent = item.secure
      ? t("status.networkSecure", { rssi: item.rssi })
      : t("status.networkOpen", { rssi: item.rssi });

    const secure = document.createElement("span");
    secure.className = "scan-meta";
    secure.textContent = item.secure ? t("status.secure") : t("status.open");

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
      setStatus(t("status.selectedWifi", { ssid: item.ssid }), "ok");
    });

    scanList.appendChild(button);
  });
}

async function scanWifi() {
  if (scanInProgress) return;

  scanInProgress = true;
  scanBtn.disabled = true;
  setStatus(t("status.scanning"), "warn");

  try {
    const result = await fetchJson("/api/scan");
    renderNetworks(result.items || []);
    setStatus(t("status.scanFound", { count: result.count ?? 0 }), "ok");
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
    alarmInfo.textContent = t("status.noAlarm");
    return;
  }

  alarmInfo.textContent = t("status.alarmSet", {
    time: alarm.time,
    message: alarm.message || t("status.noMessage"),
  });
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
      alert(t("status.alarmAlert", { message: alarm.message || t("status.checkDevice") }));
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
  langButtons.forEach((button) => {
    button.addEventListener("click", () => {
      setLanguage(button.dataset.lang);
    });
  });

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
      setStatus(t("status.enterWifiName"), "bad");
      return;
    }

    saveBtn.disabled = true;
    setStatus(t("status.testingWifi"), "warn");

    try {
      const body = new URLSearchParams();
      body.set("ssid", ssid);
      body.set("password", password);
      if (hidden) {
        body.set("hidden", "1");
      }

      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 25000);

      let result;
      try {
        result = await fetchJson("/api/wifi", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded",
          },
          body: body.toString(),
          signal: controller.signal,
        });
      } finally {
        clearTimeout(timeout);
      }

      const message = result.message || t("status.connectedSaved");
      resetWifiFormDirty();
      setStatus(message, "ok");
      if (!message.toLowerCase().includes("restart")) {
        saveBtn.disabled = false;
      }
    } catch (error) {
      if (error.name === "AbortError") {
        setStatus(t("status.requestTimedOut"), "bad");
      } else {
        setStatus(error.message, "bad");
      }
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
      setMemorySelection(t("status.noImageSelected"));
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
    setStatus(t("status.enablePortal"), "warn");

    try {
      const result = await fetchJson("/api/provisioning/start", {
        method: "POST",
      });
      setStatus(result.message || t("status.portalActive"), "ok");
      await loadState();
    } catch (error) {
      setStatus(error.message, "bad");
    } finally {
      await loadState().catch(() => {});
    }
  });

  disconnectBtn.addEventListener("click", async () => {
    disconnectBtn.disabled = true;
    setStatus(t("status.disconnectingWifi"), "warn");
    try {
      const result = await fetchJson("/api/wifi/disconnect", { method: "POST" });
      setStatus(result.message || t("status.wifiDisconnected"), "ok");
    } catch (error) {
      setStatus(error.message, "bad");
    } finally {
      disconnectBtn.disabled = false;
      await loadState().catch(() => {});
    }
  });

  forgetBtn.addEventListener("click", async () => {
    if (!confirm(t("status.eraseConfirm"))) return;
    forgetBtn.disabled = true;
    setStatus(t("status.erasingCredentials"), "warn");
    try {
      const result = await fetchJson("/api/wifi/forget", { method: "POST" });
      setStatus(result.message || t("status.credentialsErased"), "ok");
    } catch (error) {
      setStatus(error.message, "bad");
    } finally {
      forgetBtn.disabled = false;
      await loadState().catch(() => {});
    }
  });

  stopPortalBtn.addEventListener("click", async () => {
    stopPortalBtn.disabled = true;
    setStatus(t("status.stoppingPortal"), "warn");
    try {
      const result = await fetchJson("/api/provisioning/stop", { method: "POST" });
      setStatus(result.message || t("status.provisioningStopped"), "ok");
    } catch (error) {
      setStatus(error.message, "bad");
    } finally {
      stopPortalBtn.disabled = false;
      await loadState().catch(() => {});
    }
  });

  alarmSaveBtn.addEventListener("click", () => {
    const time = alarmTime.value;
    const message = alarmMessage.value.trim();

    if (!time) {
      setStatus(t("status.setAlarmFirst"), "bad");
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
    setStatus(t("status.alarmSaved"), "ok");
  });

  alarmClearBtn.addEventListener("click", () => {
    localStorage.removeItem(ALARM_STORAGE_KEY);
    alarmTime.value = "";
    alarmMessage.value = "";
    renderAlarm();
    setStatus(t("status.alarmCleared"), "ok");
  });
}

async function boot() {
  if (!ensureUiBindings()) {
    return;
  }

  setLanguage(currentLang);
  bindUiEvents();
  renderAlarm();
  scheduleAlarmCheck();
  drawMemoryPlaceholder();

  try {
    await Promise.all([loadState(), loadTelemetry(), loadMemoryPhotoState()]);
  } catch (error) {
    setStatus(error.message, "bad");
  }

  setStatus(t("status.dashboardReady"), "ok");

  setInterval(() => {
    loadState().catch((error) => setStatus(error.message, "bad"));
  }, 5000);

  setInterval(() => {
    loadTelemetry().catch(() => {});
  }, 1000);
}

boot().catch((error) => {
  setStatus(t("status.bootFailed", { message: error.message || "unknown error" }), "bad");
});
