#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <M5AtomicMotion.h>

M5AtomicMotion AtomicMotion;
WebServer server(80);

constexpr uint8_t MAGNET_COUNT = 2;
constexpr uint8_t MAGNET_CH[MAGNET_COUNT] = {0, 1};  // 0 = M1, 1 = M2

// Atomic Motion Baseへ出す最大値。
// 最初は低めにして、コイル・基板の発熱と磁力を確認する。
constexpr int MAGNET_LIMIT = 50;

// 極性を反転する直前にOFFを挟む時間。
constexpr uint16_t REVERSAL_OFF_MS = 10;

// 負：極性B、正：極性A、0：無通電
int magnetPower[MAGNET_COUNT] = {0, 0};

// false：停止、true：出力中
bool magnetRunning[MAGNET_COUNT] = {false, false};

// 実際にAtomic Motion Baseへ出している値。
// 極性反転を検知するために使用する。
int appliedOutput[MAGNET_COUNT] = {0, 0};

const char* AP_SSID = "AtomMagnet";
const char* AP_PASSWORD = "magnetcontrol";

int signOf(int value) {
  if (value > 0) return 1;
  if (value < 0) return -1;
  return 0;
}

int requestedMagnetIndex() {
  if (!server.hasArg("ch")) {
    return 0;
  }

  int ch = server.arg("ch").toInt();

  if (ch < 0 || ch >= MAGNET_COUNT) {
    return -1;
  }

  return ch;
}

void magnetOff(uint8_t index) {
  AtomicMotion.setMotorSpeed(MAGNET_CH[index], 0);
  appliedOutput[index] = 0;
}

void applyMagnet(uint8_t index) {
  int target = magnetRunning[index] ? magnetPower[index] : 0;

  target = constrain(target, -MAGNET_LIMIT, MAGNET_LIMIT);

  const int oldSign = signOf(appliedOutput[index]);
  const int newSign = signOf(target);

  // 正極性→負極性、または負極性→正極性へ切り替えるとき、
  // 一度だけ完全停止してから反転する。
  if (oldSign != 0 && newSign != 0 && oldSign != newSign) {
    AtomicMotion.setMotorSpeed(MAGNET_CH[index], 0);
    delay(REVERSAL_OFF_MS);
  }

  AtomicMotion.setMotorSpeed(MAGNET_CH[index], target);
  appliedOutput[index] = target;
}

void sendState() {
  String json = "{\"magnets\":[";

  for (uint8_t i = 0; i < MAGNET_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }

    json += "{\"ch\":";
    json += String(MAGNET_CH[i]);

    json += ",\"power\":";
    json += String(magnetPower[i]);

    json += ",\"running\":";
    json += magnetRunning[i] ? "true" : "false";

    json += "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

// 停止 ⇄ 開始
void handleToggle() {
  int index = requestedMagnetIndex();

  if (index < 0) {
    server.send(400, "text/plain", "ch must be 0 or 1");
    return;
  }

  magnetRunning[index] = !magnetRunning[index];
  applyMagnet(index);

  server.send(204, "text/plain", "");
}

// -MAGNET_LIMIT 〜 +MAGNET_LIMIT の値を受け取る
void handlePower() {
  int index = requestedMagnetIndex();

  if (index < 0) {
    server.send(400, "text/plain", "ch must be 0 or 1");
    return;
  }

  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "value is required");
    return;
  }

  magnetPower[index] = constrain(
    server.arg("value").toInt(),
    -MAGNET_LIMIT,
    MAGNET_LIMIT
  );

  applyMagnet(index);

  server.send(204, "text/plain", "");
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>電磁石制御</title>

<style>
  html,
  body {
    position: fixed;
    inset: 0;
    width: 100%;
    height: 100%;
    overflow: hidden;
    margin: 0;
    font-family: system-ui, sans-serif;
    color: #222;
    overscroll-behavior: none
  }

  main {
    height: 100dvh;
    display: grid;
    place-content: center;
    padding: 16px
  }

  .panel {
    width: min(360px, calc(100vw - 32px));
    display: grid;
    gap: 12px
  }

  .magnet {
    display: grid;
    gap: 10px;
    padding: 12px;
    border: 1px solid #ccc;
    border-radius: 6px
  }

  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px
  }

  .name {
    font-weight: 700
  }

  .status {
    color: #555
  }

  button {
    padding: 10px 16px;
    border: 1px solid #999;
    border-radius: 4px;
    background: #fff
  }

  button.running {
    background: #ddd;
    color: #222
  }

  input {
    width: 100%
  }
</style>

<main>
  <section class="panel">

    <div class="magnet" data-ch="0">
      <div class="head">
        <span class="name">電磁石 1（M1）</span>
        <span class="status">接続を確認中...</span>
      </div>

      <label>
        強度・極性:
        <output class="value">0</output>
      </label>

      <input
        class="power"
        type="range"
        min="-50"
        max="50"
        value="0"
      >

      <button class="toggle">開始</button>
    </div>

    <div class="magnet" data-ch="1">
      <div class="head">
        <span class="name">電磁石 2（M2）</span>
        <span class="status">接続を確認中...</span>
      </div>

      <label>
        強度・極性:
        <output class="value">0</output>
      </label>

      <input
        class="power"
        type="range"
        min="-50"
        max="50"
        value="0"
      >

      <button class="toggle">開始</button>
    </div>

  </section>
</main>

<script>
const magnets = [...document.querySelectorAll('.magnet')].map(element => ({
  ch: Number(element.dataset.ch),
  element,
  power: element.querySelector('.power'),
  value: element.querySelector('.value'),
  status: element.querySelector('.status'),
  toggle: element.querySelector('.toggle'),
  running: false,
  sending: false,
  latestPower: null
}));

function render(magnet) {
  magnet.value.textContent = magnet.power.value;

  if (!magnet.running) {
    magnet.status.textContent = '停止中';
  } else if (Number(magnet.power.value) === 0) {
    magnet.status.textContent = '無通電';
  } else if (Number(magnet.power.value) > 0) {
    magnet.status.textContent = '極性Aで出力中';
  } else {
    magnet.status.textContent = '極性Bで出力中';
  }

  magnet.toggle.textContent = magnet.running ? '停止' : '開始';
  magnet.toggle.classList.toggle('running', magnet.running);
}

function post(url) {
  return fetch(url, {
    method: 'POST',
    cache: 'no-store'
  });
}

magnets.forEach(magnet => {
  magnet.toggle.addEventListener('click', async () => {
    const previous = magnet.running;

    magnet.running = !magnet.running;
    render(magnet);

    try {
      const response = await post('/api/toggle?ch=' + magnet.ch);

      if (!response.ok) {
        throw new Error();
      }
    } catch (_) {
      magnet.running = previous;
      render(magnet);
      magnet.status.textContent = '通信エラー';
    }
  });

  magnet.power.addEventListener('input', () => {
    magnet.value.textContent = magnet.power.value;
    magnet.latestPower = magnet.power.value;

    if (magnet.running) {
      render(magnet);
    }

    sendLatestPower(magnet);
  });
});

// 通信中に値が変わった場合は途中値を捨て、最新の値だけ送信する
async function sendLatestPower(magnet) {
  if (magnet.sending) {
    return;
  }

  magnet.sending = true;

  while (magnet.latestPower !== null) {
    const value = magnet.latestPower;
    magnet.latestPower = null;

    try {
      const response = await post(
        '/api/power?ch=' + magnet.ch + '&value=' + value
      );

      if (!response.ok) {
        throw new Error();
      }
    } catch (_) {
      magnet.status.textContent = '通信エラー';
    }
  }

  magnet.sending = false;
}

fetch('/api/state', { cache: 'no-store' })
  .then(response => response.json())
  .then(state => {
    const states = state.magnets || [];

    states.forEach(magnetState => {
      const magnet = magnets.find(item => item.ch === magnetState.ch);

      if (!magnet) {
        return;
      }

      magnet.power.value = magnetState.power;
      magnet.running = magnetState.running;

      render(magnet);
    });
  })
  .catch(() => {
    magnets.forEach(magnet => {
      magnet.status.textContent = '通信エラー';
    });
  });
</script>
)HTML";

void setup() {
  Serial.begin(115200);
  delay(100);

  // AtomS3 Lite + Atomic Motion Base
  // SDA = GPIO 38, SCL = GPIO 39
  while (!AtomicMotion.begin(
    &Wire,
    M5_ATOMIC_MOTION_I2C_ADDR,
    38,
    39,
    100000
  )) {
    Serial.println("Atomic Motion Base not found.");
    delay(1000);
  }

  // 起動時は必ず全チャンネルを停止する
  for (uint8_t i = 0; i < MAGNET_COUNT; i++) {
    magnetOff(i);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/toggle", HTTP_POST, handleToggle);
  server.on("/api/power", HTTP_POST, handlePower);

  server.begin();

  Serial.printf(
    "Open: http://%s/\n",
    WiFi.softAPIP().toString().c_str()
  );
}

void loop() {
  server.handleClient();
}