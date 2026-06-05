// ============================================================
//  api_server.cpp - REST API + WebSocket implementation
// ============================================================
#include "api_server.h"
#include "sha256d.h"
#include "stats.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Update.h>

namespace WroomMiner {

namespace {

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WroomMiner</title>
<style>
:root{color-scheme:dark;--bg:#101418;--panel:#171d22;--line:#2c3740;--text:#eef4f0;--muted:#9aa9a2;--green:#5dd39e;--amber:#f4b860;--blue:#75b8ff;--red:#ff6b6b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}main{max-width:1040px;margin:0 auto;padding:20px}header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.mark{width:38px;height:38px;border:2px solid var(--green);display:grid;place-items:center;color:var(--green);font-weight:800}.title h1{font-size:22px;margin:0}.title p{margin:1px 0 0;color:var(--muted)}.state{display:flex;align-items:center;gap:8px;color:var(--muted)}.dot{width:10px;height:10px;background:var(--red);border-radius:50%}.dot.ok{background:var(--green)}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:14px}.card,.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px}.card{padding:12px}.label{color:var(--muted);font-size:12px}.value{font-size:22px;font-weight:750;margin-top:4px}.subvalue{color:var(--muted);font-size:12px;margin-top:3px}.unit{font-size:13px;color:var(--muted)}.layout{display:grid;grid-template-columns:1.1fr .9fr;gap:14px}.panel{padding:14px}h2{font-size:15px;margin:0 0 12px}.fields{display:grid;grid-template-columns:1fr 110px;gap:10px}.full{grid-column:1/-1}label{display:grid;gap:5px;color:var(--muted);font-size:12px}input{width:100%;border:1px solid var(--line);border-radius:6px;background:#0d1115;color:var(--text);padding:10px;font:inherit}input:focus{outline:1px solid var(--blue)}.toggle{display:flex;align-items:center;gap:8px}.toggle input{width:auto}.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}button{border:1px solid var(--line);border-radius:6px;background:#22303a;color:var(--text);padding:10px 12px;font:inherit;cursor:pointer}button.primary{background:#1f5f47;border-color:#2d8a68}button.warn{background:#5a3d1c;border-color:#9a682d}.rows{display:grid;gap:8px}.row{display:flex;justify-content:space-between;gap:12px;border-bottom:1px solid var(--line);padding-bottom:7px}.row:last-child{border-bottom:0;padding-bottom:0}.row span:last-child{text-align:right;overflow-wrap:anywhere}.status{min-height:20px;color:var(--amber);margin-top:10px}@media (max-width:820px){main{padding:12px}.grid,.layout{grid-template-columns:1fr 1fr}.layout{display:block}.panel{margin-bottom:12px}.fields{grid-template-columns:1fr}}@media (max-width:520px){header{align-items:flex-start}.grid{grid-template-columns:1fr}.fields{grid-template-columns:1fr}.value{font-size:20px}}
</style>
</head>
<body>
<main>
<header>
  <div class="brand"><div class="mark">W</div><div class="title"><h1>WroomMiner</h1><p id="version">loading</p></div></div>
  <div class="state"><span id="wifiDot" class="dot"></span><span id="wifiText">offline</span></div>
</header>
<section class="grid">
  <div class="card"><div class="label">Hashrate 1s</div><div class="value"><span id="hash1s">0 H/s</span></div></div>
  <div class="card"><div class="label">Hashrate 1m</div><div class="value"><span id="hash1m">0 H/s</span></div></div>
  <div class="card"><div class="label">Shares</div><div class="value"><span id="sharesAccepted">0</span><span class="unit"> ok</span></div><div class="subvalue"><span id="sharesDetail">0 submitted / 0 rejected</span></div></div>
  <div class="card"><div class="label">Best difficulty</div><div class="value"><span id="bestDiff">0</span></div><div class="subvalue">Last share <span id="lastShare">never</span></div></div>
</section>
<section class="layout">
  <div class="panel">
    <h2>Configuration</h2>
    <form id="configForm">
      <div class="fields">
        <label class="full">Primary wallet address<input id="wallet_address" name="wallet_address" autocomplete="off"></label>
        <label class="full">Worker<input id="worker_name" name="worker_name"></label>
        <label>Primary pool<input id="pool_primary_url" name="pool_primary_url"></label>
        <label>Port<input id="pool_primary_port" name="pool_primary_port" type="number" min="1" max="65535"></label>
        <label class="full">Primary pool password<input id="pool_primary_password" name="pool_primary_password" placeholder="unchanged"></label>
        <label class="full">Primary suggested difficulty<input id="pool_primary_suggest_difficulty" name="pool_primary_suggest_difficulty" type="number" min="0" step="0.00001"></label>
        <label class="full">Primary submit floor<input id="pool_primary_min_submit_difficulty" name="pool_primary_min_submit_difficulty" type="number" min="0" step="0.00001"></label>
        <label>Fallback pool<input id="pool_fallback_url" name="pool_fallback_url"></label>
        <label>Port<input id="pool_fallback_port" name="pool_fallback_port" type="number" min="1" max="65535"></label>
        <label class="full">Fallback wallet address<input id="wallet_fallback_address" name="wallet_fallback_address" autocomplete="off"></label>
        <label class="full">Fallback pool password<input id="pool_fallback_password" name="pool_fallback_password" placeholder="unchanged"></label>
        <label class="full">Fallback suggested difficulty<input id="pool_fallback_suggest_difficulty" name="pool_fallback_suggest_difficulty" type="number" min="0" step="0.00001"></label>
        <label class="full">Fallback submit floor<input id="pool_fallback_min_submit_difficulty" name="pool_fallback_min_submit_difficulty" type="number" min="0" step="0.00001"></label>
        <label>UDP broadcast sec<input id="udp_broadcast_sec" name="udp_broadcast_sec" type="number" min="0" max="255"></label>
        <label>API port<input id="api_port" name="api_port" type="number" min="1" max="65535"></label>
        <label class="toggle full"><input id="led_enabled" name="led_enabled" type="checkbox"> LED enabled</label>
      </div>
      <div class="actions"><button class="primary" type="submit">Save</button><button type="button" id="restartBtn">Restart</button><button type="button" class="warn" id="resetBtn">Factory reset</button></div>
      <div class="status" id="formStatus"></div>
    </form>
    <h2>Firmware update</h2>
    <input id="otaFile" type="file" accept=".bin">
    <div class="actions"><button type="button" id="otaBtn">Upload OTA</button></div>
    <div class="status" id="otaStatus"></div>
  </div>
  <div class="panel">
    <h2>Device</h2>
    <div class="rows">
      <div class="row"><span class="label">IP</span><span id="ip">-</span></div>
      <div class="row"><span class="label">SSID</span><span id="ssid">-</span></div>
      <div class="row"><span class="label">RSSI</span><span id="rssi">-</span></div>
      <div class="row"><span class="label">Active pool</span><span id="pool">-</span></div>
      <div class="row"><span class="label">Active wallet</span><span id="activeWallet">-</span></div>
      <div class="row"><span class="label">Uptime</span><span id="uptime">0s</span></div>
      <div class="row"><span class="label">Free heap</span><span id="heap">-</span></div>
    </div>
  </div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const fmt=n=>Number(n||0).toLocaleString(undefined,{maximumFractionDigits:1});
const fmtHashrate=n=>{n=Number(n||0);if(n>=1e6)return (n/1e6).toLocaleString(undefined,{maximumFractionDigits:4})+' MH/s';if(n>=1e3)return (n/1e3).toLocaleString(undefined,{maximumFractionDigits:2})+' KH/s';return n.toLocaleString(undefined,{maximumFractionDigits:1})+' H/s';};
const fmtAge=s=>{s=Number(s||0);if(s<=0)return 'never';if(s<60)return Math.floor(s)+'s ago';if(s<3600)return Math.floor(s/60)+'m '+Math.floor(s%60)+'s ago';return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m ago';};
async function getJson(url){const r=await fetch(url);if(!r.ok)throw new Error(url);return r.json();}
function setVal(id,v){const e=$(id);if(e)e.value=v??'';}
function fillConfig(c){['wallet_address','wallet_fallback_address','worker_name','pool_primary_url','pool_primary_port','pool_primary_suggest_difficulty','pool_primary_min_submit_difficulty','pool_fallback_url','pool_fallback_port','pool_fallback_suggest_difficulty','pool_fallback_min_submit_difficulty','udp_broadcast_sec','api_port'].forEach(k=>setVal(k,c[k]));$('led_enabled').checked=!!c.led_enabled;}
async function refresh(){
 try{
  const [status,mining,network,pool,info]=await Promise.all([getJson('/api/status'),getJson('/api/mining'),getJson('/api/network'),getJson('/api/pool'),getJson('/api/info')]);
  $('version').textContent=info.version+' | '+info.compatible_with;
  $('wifiDot').className='dot '+(status.wifi_connected?'ok':'');
  $('wifiText').textContent=status.wifi_connected?'online':'offline';
  $('hash1s').textContent=fmtHashrate(mining.hashrate_1s_hs);$('hash1m').textContent=fmtHashrate(mining.hashrate_1m_hs);
  const accepted=mining.shares_accepted||0,rejected=mining.shares_rejected||0,submitted=mining.shares_submitted??(accepted+rejected);
  $('sharesAccepted').textContent=accepted;$('sharesDetail').textContent=submitted+' submitted / '+rejected+' rejected';$('bestDiff').textContent=fmt(mining.best_difficulty);$('lastShare').textContent=fmtAge(mining.last_share_ago_seconds);
  $('ip').textContent=network.ip||'-';$('ssid').textContent=network.ssid||'-';$('rssi').textContent=(network.rssi||0)+' dBm';
  $('pool').textContent=pool.connected?(pool.active_kind+' '+pool.active_url+':'+pool.active_port):'disconnected';$('activeWallet').textContent=pool.active_wallet||'-';$('uptime').textContent=(mining.uptime_seconds||0)+'s';$('heap').textContent=fmt(status.free_heap);
 }catch(e){}
}
async function load(){fillConfig(await getJson('/api/config'));refresh();}
$('configForm').addEventListener('submit',async e=>{
 e.preventDefault();const f=e.currentTarget;const body={};
 ['wallet_address','wallet_fallback_address','worker_name','pool_primary_url','pool_fallback_url'].forEach(k=>body[k]=f[k].value.trim());
 ['pool_primary_port','pool_fallback_port','udp_broadcast_sec','api_port'].forEach(k=>body[k]=Number(f[k].value));
 ['pool_primary_suggest_difficulty','pool_primary_min_submit_difficulty','pool_fallback_suggest_difficulty','pool_fallback_min_submit_difficulty'].forEach(k=>body[k]=Number(f[k].value));
 if(f.pool_primary_password.value.trim())body.pool_primary_password=f.pool_primary_password.value.trim();
 if(f.pool_fallback_password.value.trim())body.pool_fallback_password=f.pool_fallback_password.value.trim();
 body.led_enabled=f.led_enabled.checked;
 const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 $('formStatus').textContent=r.ok?'Saved. Restart required.':'Save failed.';
});
$('restartBtn').onclick=()=>fetch('/api/system/restart',{method:'POST'});
$('resetBtn').onclick=()=>{if(confirm('Factory reset?'))fetch('/api/system/reset',{method:'POST'});};
$('otaBtn').onclick=async()=>{
 const file=$('otaFile').files[0];if(!file){$('otaStatus').textContent='Select a firmware .bin first.';return;}
 const body=new FormData();body.append('firmware',file,file.name);$('otaStatus').textContent='Uploading...';
 const r=await fetch('/api/ota',{method:'POST',body});$('otaStatus').textContent=r.ok?'Uploaded. Restarting...':'OTA failed.';
};
load();setInterval(refresh,1000);
</script>
</body>
</html>
)HTML";

} // namespace

ApiServer::ApiServer() {}

void ApiServer::start(MinerConfig* cfg) {
    _cfg = cfg;
    _server = new AsyncWebServer(cfg->apiPort);
    _ws     = new AsyncWebSocket("/ws");

    _ws->onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                        AwsEventType t, void* arg, uint8_t* d, size_t l) {
        onWsEvent(s, c, t, arg, d, l);
    });
    _server->addHandler(_ws);

    setupRoutes();
    _server->begin();
    log_i("API: server started on port %u", cfg->apiPort);
}

void ApiServer::stop() {
    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
    if (_ws) {
        delete _ws;
        _ws = nullptr;
    }
}

void ApiServer::setPoolState(const String& activeKind,
                             const String& activeUrl,
                             uint16_t activePort,
                             bool connected) {
    _activePoolKind = activeKind;
    _activePoolUrl = activeUrl;
    _activePoolPort = activePort;
    _poolConnected = connected;
}

void ApiServer::setupRoutes() {
    // --- Root ---
    _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", INDEX_HTML);
    });

    // --- Discovery ---
    _server->on("/api/probe", HTTP_GET, [this](AsyncWebServerRequest* r){ handleProbe(r); });

    // --- GET endpoints ---
    _server->on("/api/status",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleStatus(r); });
    _server->on("/api/mining",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleMining(r); });
    _server->on("/api/pool",    HTTP_GET, [this](AsyncWebServerRequest* r){ handlePool(r); });
    _server->on("/api/network", HTTP_GET, [this](AsyncWebServerRequest* r){ handleNetwork(r); });
    _server->on("/api/system",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleSystem(r); });
    _server->on("/api/config",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleConfigGet(r); });
    _server->on("/api/stats",   HTTP_GET, [this](AsyncWebServerRequest* r){ handleStats(r); });
    _server->on("/api/info",    HTTP_GET, [this](AsyncWebServerRequest* r){ handleInfo(r); });

    // --- POST endpoints ---
    auto* configPost = new AsyncCallbackJsonWebHandler("/api/config",
        [this](AsyncWebServerRequest* r, JsonVariant& body) { handleConfigPost(r, body); });
    _server->addHandler(configPost);

    _server->on("/api/system/restart", HTTP_POST,
                [this](AsyncWebServerRequest* r){ handleRestart(r); });
    _server->on("/api/system/reset",   HTTP_POST,
                [this](AsyncWebServerRequest* r){ handleFactoryReset(r); });
    _server->on("/api/ota", HTTP_POST,
                [this](AsyncWebServerRequest* r){ handleOtaPost(r); },
                [this](AsyncWebServerRequest* r, const String& filename,
                       size_t index, uint8_t* data, size_t len, bool final) {
                    handleOtaUpload(r, filename, index, data, len, final);
                });

    // --- Legacy /api/v1/* redirects (301) ---
    _server->on("/api/v1/status",         HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/status"); });
    _server->on("/api/v1/mining",         HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/mining"); });
    _server->on("/api/v1/pool",           HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/pool"); });
    _server->on("/api/v1/network",        HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/network"); });
    _server->on("/api/v1/system",         HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/system"); });
    _server->on("/api/v1/config",         HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/config"); });
    _server->on("/api/v1/stats",          HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/stats"); });
    _server->on("/api/v1/info",           HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/info"); });
    _server->on("/probe",                 HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/probe"); });
    _server->on("/api/system/info",       HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/api/system"); });
    _server->on("/api/v1/action/restart", HTTP_POST,[this](AsyncWebServerRequest* r){ handleRestart(r); });
    _server->on("/api/v1/action/reset",   HTTP_POST,[this](AsyncWebServerRequest* r){ handleFactoryReset(r); });

    // 404
    _server->onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
}

// ============================================================
//  GET handlers
// ============================================================

void ApiServer::handleStatus(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    auto& s = Stats::get();
    root["uptime_seconds"]    = Stats::uptimeSeconds();
    root["hashrate_hs"]       = s.hashrate1m.load();
    root["shares_accepted"]   = s.sharesAccepted.load();
    root["shares_rejected"]   = s.sharesRejected.load();
    root["best_difficulty"]   = s.bestDifficulty.load();
    root["wifi_connected"]    = WiFi.status() == WL_CONNECTED;
    root["wifi_rssi"]         = WiFi.RSSI();
    root["free_heap"]         = ESP.getFreeHeap();
    root["firmware_version"]  = WROOMMINER_VERSION;

    res->setLength();
    req->send(res);
}

void ApiServer::handleMining(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    auto& s = Stats::get();
    root["hashrate_1s_hs"]    = s.hashrate1s.load();
    root["hashrate_1m_hs"]    = s.hashrate1m.load();
    root["total_hashes"]      = s.hashCount.load();
    root["shares_accepted"]   = s.sharesAccepted.load();
    root["shares_rejected"]   = s.sharesRejected.load();
    root["shares_submitted"]  = s.sharesAccepted.load() + s.sharesRejected.load();
    root["blocks_found"]      = s.blocksFound.load();
    root["best_difficulty"]   = s.bestDifficulty.load();
    root["session_best_diff"] = s.sessionBestDiff.load();
    root["uptime_seconds"]    = Stats::uptimeSeconds();
    root["last_share_ago_ms"] = s.lastShareMillis ? (millis() - s.lastShareMillis) : 0;
    root["last_share_ago_seconds"] = s.lastShareMillis ? ((millis() - s.lastShareMillis) / 1000) : 0;

    res->setLength();
    req->send(res);
}

void ApiServer::handlePool(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    JsonObject primary = root["primary"].to<JsonObject>();
    primary["url"]  = _cfg->poolPrimaryUrl;
    primary["port"] = _cfg->poolPrimaryPort;

    JsonObject fallback = root["fallback"].to<JsonObject>();
    fallback["url"]  = _cfg->poolFallbackUrl;
    fallback["port"] = _cfg->poolFallbackPort;

    String wallet = _activePoolKind == "fallback" && _cfg->walletFallbackAddress.length() > 0
        ? _cfg->walletFallbackAddress
        : _cfg->walletAddress;
    String fullWorker = wallet;
    if (_cfg->workerName.length() > 0) {
        fullWorker += "." + _cfg->workerName;
    }
    String activePool = _poolConnected && _activePoolUrl.length() > 0
        ? _activePoolUrl + ":" + String(_activePoolPort)
        : "";

    root["active"]        = _activePoolKind;
    root["active_url"]    = _activePoolUrl;
    root["active_port"]   = _activePoolPort;
    root["connected"]     = _poolConnected;
    root["worker"]        = fullWorker;
    root["active_wallet"] = wallet;

    res->setLength();
    req->send(res);
}

void ApiServer::handleNetwork(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["ssid"]         = WiFi.SSID();
    root["rssi"]         = WiFi.RSSI();
    root["ip"]           = WiFi.localIP().toString();
    root["mac"]          = WiFi.macAddress();
    root["gateway"]      = WiFi.gatewayIP().toString();
    root["dns"]          = WiFi.dnsIP().toString();
    root["channel"]      = WiFi.channel();
    root["hostname"]     = WiFi.getHostname();

    res->setLength();
    req->send(res);
}

void ApiServer::handleSystem(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);

    root["chip_model"]   = (chipInfo.model == CHIP_ESP32) ? "ESP32" :
                           (chipInfo.model == CHIP_ESP32S2) ? "ESP32-S2" :
                           (chipInfo.model == CHIP_ESP32S3) ? "ESP32-S3" :
                           (chipInfo.model == CHIP_ESP32C3) ? "ESP32-C3" : "Unknown";
    root["chip_cores"]      = chipInfo.cores;
    root["chip_revision"]   = chipInfo.revision;
    root["cpu_freq_mhz"]    = ESP.getCpuFreqMHz();
    root["flash_size"]      = ESP.getFlashChipSize();
    root["sketch_size"]     = ESP.getSketchSize();
    root["sketch_free"]     = ESP.getFreeSketchSpace();
    root["free_heap"]       = ESP.getFreeHeap();
    root["min_free_heap"]   = ESP.getMinFreeHeap();
    root["heap_size"]       = ESP.getHeapSize();
    root["idf_version"]     = esp_get_idf_version();
    root["temperature_c"]   = -1; // ESP32-WROOM-32D has no internal temp sensor
    root["uptime_seconds"]  = Stats::uptimeSeconds();

    res->setLength();
    req->send(res);
}

void ApiServer::handleConfigGet(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["pool_primary_url"]   = _cfg->poolPrimaryUrl;
    root["pool_primary_port"]  = _cfg->poolPrimaryPort;
    root["pool_primary_suggest_difficulty"] = _cfg->poolPrimarySuggestDiff;
    root["pool_primary_min_submit_difficulty"] = _cfg->poolPrimaryMinSubmitDiff;
    root["pool_fallback_url"]  = _cfg->poolFallbackUrl;
    root["pool_fallback_port"] = _cfg->poolFallbackPort;
    root["pool_fallback_suggest_difficulty"] = _cfg->poolFallbackSuggestDiff;
    root["pool_fallback_min_submit_difficulty"] = _cfg->poolFallbackMinSubmitDiff;
    root["wallet_address"]     = _cfg->walletAddress;
    root["wallet_fallback_address"] = _cfg->walletFallbackAddress;
    root["worker_name"]        = _cfg->workerName;
    root["wifi_ssid"]          = _cfg->wifiSsid;
    root["led_enabled"]        = _cfg->ledEnabled;
    root["udp_broadcast_sec"]  = _cfg->udpBroadcastSec;
    root["api_port"]           = _cfg->apiPort;
    // pool passwords intentionally NOT returned

    res->setLength();
    req->send(res);
}

void ApiServer::handleStats(AsyncWebServerRequest* req) {
    // Extended statistics with all details
    handleMining(req); // Phase 1: same as /mining
}

void ApiServer::handleInfo(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["firmware"]        = WROOMMINER_NAME;
    root["version"]         = WROOMMINER_VERSION;
    root["build_date"]      = __DATE__ " " __TIME__;
    root["api_version"]     = "1";
    root["sha256d_backend"] = sha256d_backend_name();

    res->setLength();
    req->send(res);
}

void ApiServer::handleProbe(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    auto& s = Stats::get();
    root["firmware"]        = WROOMMINER_NAME;
    root["version"]         = WROOMMINER_VERSION;
    root["hostname"]        = WiFi.getHostname();
    root["mac"]             = WiFi.macAddress();
    root["ip"]              = WiFi.localIP().toString();
    root["uptime_seconds"]  = Stats::uptimeSeconds();
    root["hashrate_hs"]     = s.hashrate1s.load();
    root["hashrate_1m_hs"]  = s.hashrate1m.load();
    root["shares_accepted"] = s.sharesAccepted.load();
    root["best_difficulty"] = s.bestDifficulty.load();

    res->setLength();
    req->send(res);
}

// ============================================================
//  POST handlers
// ============================================================

void ApiServer::handleConfigPost(AsyncWebServerRequest* req, JsonVariant& body) {
    JsonObject in = body.as<JsonObject>();
    if (in.isNull()) {
        req->send(400, "application/json", "{\"error\":\"invalid_json\"}");
        return;
    }

    bool changed = false;
    if (in["pool_primary_url"].is<const char*>()) {
        _cfg->poolPrimaryUrl = in["pool_primary_url"].as<String>(); changed = true;
    }
    if (in["pool_primary_port"].is<int>()) {
        _cfg->poolPrimaryPort = in["pool_primary_port"]; changed = true;
    }
    if (in["pool_fallback_url"].is<const char*>()) {
        _cfg->poolFallbackUrl = in["pool_fallback_url"].as<String>(); changed = true;
    }
    if (in["pool_fallback_port"].is<int>()) {
        _cfg->poolFallbackPort = in["pool_fallback_port"]; changed = true;
    }
    if (in["pool_primary_suggest_difficulty"].is<double>() || in["pool_primary_suggest_difficulty"].is<int>()) {
        double value = in["pool_primary_suggest_difficulty"].as<double>();
        if (value >= 0.0) {
            _cfg->poolPrimarySuggestDiff = value; changed = true;
        }
    }
    if (in["pool_fallback_suggest_difficulty"].is<double>() || in["pool_fallback_suggest_difficulty"].is<int>()) {
        double value = in["pool_fallback_suggest_difficulty"].as<double>();
        if (value >= 0.0) {
            _cfg->poolFallbackSuggestDiff = value; changed = true;
        }
    }
    if (in["pool_primary_min_submit_difficulty"].is<double>() || in["pool_primary_min_submit_difficulty"].is<int>()) {
        double value = in["pool_primary_min_submit_difficulty"].as<double>();
        if (value >= 0.0) {
            _cfg->poolPrimaryMinSubmitDiff = value; changed = true;
        }
    }
    if (in["pool_fallback_min_submit_difficulty"].is<double>() || in["pool_fallback_min_submit_difficulty"].is<int>()) {
        double value = in["pool_fallback_min_submit_difficulty"].as<double>();
        if (value >= 0.0) {
            _cfg->poolFallbackMinSubmitDiff = value; changed = true;
        }
    }
    if (in["wallet_address"].is<const char*>()) {
        _cfg->walletAddress = in["wallet_address"].as<String>(); changed = true;
    }
    if (in["wallet_fallback_address"].is<const char*>()) {
        _cfg->walletFallbackAddress = in["wallet_fallback_address"].as<String>(); changed = true;
    }
    if (in["worker_name"].is<const char*>()) {
        _cfg->workerName = in["worker_name"].as<String>(); changed = true;
    }
    if (in["pool_primary_password"].is<const char*>()) {
        _cfg->poolPrimaryPassword = in["pool_primary_password"].as<String>(); changed = true;
    }
    if (in["pool_fallback_password"].is<const char*>()) {
        _cfg->poolFallbackPassword = in["pool_fallback_password"].as<String>(); changed = true;
    }
    if (in["led_enabled"].is<bool>()) {
        _cfg->ledEnabled = in["led_enabled"]; changed = true;
    }
    if (in["udp_broadcast_sec"].is<int>()) {
        int value = in["udp_broadcast_sec"];
        if (value >= 0 && value <= 255) {
            _cfg->udpBroadcastSec = uint8_t(value); changed = true;
        }
    }
    if (in["api_port"].is<int>()) {
        int value = in["api_port"];
        if (value > 0 && value <= 65535) {
            _cfg->apiPort = uint16_t(value); changed = true;
        }
    }

    if (changed) {
        Config::save(*_cfg);
        req->send(200, "application/json", "{\"status\":\"saved\",\"restart_required\":true}");
    } else {
        req->send(400, "application/json", "{\"error\":\"no_valid_fields\"}");
    }
}

void ApiServer::handleOtaPost(AsyncWebServerRequest* req) {
    if (_otaFailed || Update.hasError()) {
        req->send(500, "application/json", "{\"status\":\"error\",\"error\":\"ota_failed\"}");
        _otaFailed = false;
        _otaBytes = 0;
        Update.abort();
        return;
    }

    char buf[96];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"bytes\":%u,\"will_restart\":true}",
             static_cast<unsigned>(_otaBytes));
    req->send(200, "application/json", buf);
    _otaBytes = 0;
    delay(500);
    ESP.restart();
}

void ApiServer::handleOtaUpload(AsyncWebServerRequest* /*req*/,
                                const String& filename,
                                size_t index,
                                uint8_t* data,
                                size_t len,
                                bool final) {
    if (index == 0) {
        _otaFailed = false;
        _otaBytes = 0;
        Serial.printf("[ota] upload started: %s\r\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            _otaFailed = true;
            Update.printError(Serial);
            return;
        }
    }

    if (_otaFailed) {
        return;
    }

    if (Update.write(data, len) != len) {
        _otaFailed = true;
        Update.printError(Serial);
        return;
    }
    _otaBytes += len;

    if (final) {
        if (!Update.end(true)) {
            _otaFailed = true;
            Update.printError(Serial);
            return;
        }
        Serial.printf("[ota] upload complete: %u bytes\r\n", static_cast<unsigned>(_otaBytes));
    }
}

void ApiServer::handleRestart(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

void ApiServer::handleFactoryReset(AsyncWebServerRequest* req) {
    Config::factoryReset();
    req->send(200, "application/json", "{\"status\":\"factory_reset_done\",\"will_restart\":true}");
    delay(500);
    ESP.restart();
}

// ============================================================
//  WebSocket
// ============================================================

void ApiServer::onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
                          AwsEventType type, void* /*arg*/, uint8_t* /*data*/, size_t /*len*/) {
    if (type == WS_EVT_CONNECT) {
        log_i("WS: client %u connected from %s",
              client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        log_i("WS: client %u disconnected", client->id());
    }
}

void ApiServer::tick() {
    if (!_ws || _ws->count() == 0) return;

    uint32_t now = millis();
    if (now - _lastBroadcastMs < 1000) return;
    _lastBroadcastMs = now;

    auto& s = Stats::get();
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"tick\","
        "\"hashrate_hs\":%.1f,"
        "\"hashrate_1m_hs\":%.1f,"
        "\"shares_accepted\":%u,"
        "\"shares_rejected\":%u,"
        "\"best_diff\":%.6f,"
        "\"uptime_s\":%u}",
        s.hashrate1s.load(),
        s.hashrate1m.load(),
        s.sharesAccepted.load(),
        s.sharesRejected.load(),
        s.bestDifficulty.load(),
        Stats::uptimeSeconds()
    );
    _ws->textAll(buf);

    // Clean up dead clients
    _ws->cleanupClients();
}

} // namespace WroomMiner
