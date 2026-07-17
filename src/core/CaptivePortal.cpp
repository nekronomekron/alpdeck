#include "core/CaptivePortal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
DNSServer dns;
WebServer server(80);

constexpr uint16_t kDnsPort = 53;

// Mirrors the bootscreen: black on white, 2px rules, hard rectangles, and the
// same checkerboard dither the progress bar fills with.
// {{NAME}} and {{SUB}} are filled from AppConfig by handleRoot().
const char kPageHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{NAME}}</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#fff;color:#000;font:14px/1.45 ui-monospace,Menlo,Consolas,"Courier New",monospace;
padding:22px 14px 40px;max-width:460px;margin:0 auto}
h1{font-size:34px;letter-spacing:3px;text-align:center}
.sub{font-size:10px;letter-spacing:4px;margin-top:3px;text-align:center}
.rule{height:2px;background:#000;margin:12px 0 20px}
h2{font-size:11px;letter-spacing:3px;margin:0 0 10px}
button,input{font:inherit;color:#000;background:#fff;border:2px solid #000;border-radius:0;
width:100%;padding:10px;outline:none}
button{cursor:pointer;letter-spacing:2px;text-transform:uppercase}
button:active,button:focus{background:#000;color:#fff}
input:focus{border-style:double}
input:disabled{border-style:dashed;background:#fff}
::placeholder{color:#000;opacity:.45}
.net{display:flex;justify-content:space-between;gap:10px;text-align:left;
text-transform:none;letter-spacing:0;margin-bottom:-2px}
.net:hover{background:#000;color:#fff}
.net .n{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net .m{white-space:pre;font-size:12px}
label{font-size:10px;letter-spacing:2px;display:block;margin:14px 0 4px}
.dither{height:8px;margin:22px 0;
background-image:repeating-conic-gradient(#000 0 25%,#fff 0 50%);background-size:4px 4px}
#st{border:2px solid #000;padding:10px;margin-top:18px;font-size:12px;display:none;
white-space:pre-line}
.muted{font-size:11px;margin-top:8px}
</style></head><body>
<h1>{{NAME}}</h1><div class="sub">{{SUB}}</div>
<div class="rule"></div>
<h2>WIFI SETUP</h2>
<button id="rescan">Scan</button>
<div id="nets" class="muted">scanning...</div>
<div class="dither"></div>
<form id="f">
<label for="s">NETWORK</label><input id="s" name="s" maxlength="32" required>
<label for="p">PASSWORD</label><input id="p" name="p" type="password" maxlength="63">
<label></label><button type="submit">Connect</button>
</form>
<div id="st"></div>
<script>
var N=document.getElementById('nets'),S=document.getElementById('s'),
P=document.getElementById('p'),ST=document.getElementById('st'),poll=0;
function needPass(on){P.disabled=!on;if(!on)P.value='';
P.placeholder=on?'':'no password needed'}
S.oninput=function(){needPass(1)};
function bars(r){var n=r>=-55?4:r>=-65?3:r>=-75?2:1;
return'▮'.repeat(n)+'▯'.repeat(4-n)}
function scan(){N.textContent='scanning...';
fetch('/scan').then(function(r){return r.json()}).then(function(d){
if(!d.length){N.textContent='no networks found';return}
N.innerHTML='';d.forEach(function(x){
var b=document.createElement('button');b.type='button';b.className='net';
b.innerHTML='<span class="n"></span><span class="m"></span>';
b.querySelector('.n').textContent=x.s;
b.querySelector('.m').textContent=bars(x.r)+(x.l?' LOCK':'     ');
b.onclick=function(){S.value=x.s;needPass(x.l);if(x.l)P.focus()};
N.appendChild(b)})}).catch(function(){N.textContent='scan failed'})}
document.getElementById('rescan').onclick=scan;
document.getElementById('f').onsubmit=function(e){e.preventDefault();
ST.style.display='block';ST.textContent='connecting...';
fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'s='+encodeURIComponent(S.value)+'&p='+encodeURIComponent(P.value)})
.then(function(){if(!poll)poll=setInterval(check,1000)})};
function check(){fetch('/status').then(function(r){return r.json()}).then(function(d){
if(d.state=='connected'){clearInterval(poll);poll=0;
ST.textContent='connected to '+d.ssid+'\nip '+d.ip+'\nthis network will now close.'}
else if(d.state=='failed'){clearInterval(poll);poll=0;
ST.textContent='could not connect. check the password and try again.'}
else{ST.textContent='connecting...'}})}
scan();
</script></body></html>)HTML";

String jsonEscape(const String& raw) {
    String out;
    out.reserve(raw.length() + 8);
    for (size_t i = 0; i < raw.length(); i++) {
        const char c = raw[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c >= 0 && c < 0x20) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
        } else {
            out += c;
        }
    }
    return out;
}
}  // namespace

bool CaptivePortal::_active = false;
std::function<void(const String&, const String&)> CaptivePortal::_onSubmit;
std::function<String()> CaptivePortal::_onStatus;

void CaptivePortal::begin(const char* apSsid, const char* apPassword) {
    if (_active) {
        return;
    }

    // AP_STA rather than AP: the station interface is what scans, and dropping
    // it would leave the portal unable to list any networks.
    WiFi.mode(WIFI_AP_STA);
    const bool open = apPassword == nullptr || strlen(apPassword) == 0;
    WiFi.softAP(apSsid, open ? nullptr : apPassword);

    const IPAddress ip = WiFi.softAPIP();

    // Wildcard DNS: every lookup resolves here, which is what makes the OS
    // captive-portal probes fail and trigger the sign-in sheet.
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(kDnsPort, "*", ip);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/scan", HTTP_GET, handleScan);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/status", HTTP_GET, handleStatus);

    // Probe URLs the major platforms fetch to decide whether a network is
    // captive. Answering with a redirect is what pops the login sheet.
    server.on("/generate_204", HTTP_GET, handleRedirect);   // Android
    server.on("/gen_204", HTTP_GET, handleRedirect);        // Android
    server.on("/hotspot-detect.html", HTTP_GET, handleRedirect);  // Apple
    server.on("/library/test/success.html", HTTP_GET, handleRedirect);
    server.on("/ncsi.txt", HTTP_GET, handleRedirect);       // Windows
    server.on("/connecttest.txt", HTTP_GET, handleRedirect);
    server.on("/fwlink", HTTP_GET, handleRedirect);
    server.onNotFound(handleRedirect);

    server.begin();
    _active = true;

    // Kick off a scan so the first page load usually has results ready.
    WiFi.scanNetworks(true);

    LOGI(kLogTag, "Portal up: SSID '%s'%s at %s", apSsid,
         open ? " (open)" : "", ip.toString().c_str());
}

void CaptivePortal::stop() {
    if (!_active) {
        return;
    }

    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _active = false;

    LOGI(kLogTag, "Portal stopped");
}

void CaptivePortal::loop() {
    if (!_active) {
        return;
    }
    dns.processNextRequest();
    server.handleClient();
}

bool CaptivePortal::isActive() { return _active; }

void CaptivePortal::onSubmit(
    std::function<void(const String&, const String&)> cb) {
    _onSubmit = std::move(cb);
}

void CaptivePortal::onStatus(std::function<String()> cb) {
    _onStatus = std::move(cb);
}

void CaptivePortal::handleRoot() {
    // Substituted per request rather than baked in: the page is the only place
    // the branding appears outside the bootscreen, and both read AppConfig.
    String page = FPSTR(kPageHtml);
    page.replace("{{NAME}}", Config::APP_NAME);
    page.replace("{{SUB}}", Config::APP_SUBTITLE);
    server.send(200, "text/html", page);
}

void CaptivePortal::handleScan() {
    const int16_t status = WiFi.scanComplete();

    if (status == WIFI_SCAN_RUNNING) {
        // Report empty rather than blocking; the page can rescan.
        server.send(200, "application/json", "[]");
        return;
    }
    if (status == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        server.send(200, "application/json", "[]");
        return;
    }

    // One AP usually shows up several times: once per band (2.4/5 GHz) and once
    // per mesh node, each a separate BSSID sharing the SSID. Collapse them to
    // the strongest entry per name, which is the one worth connecting to.
    std::vector<int16_t> unique;
    for (int16_t i = 0; i < status; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) {
            continue;  // hidden network: nothing to label or tap
        }

        bool merged = false;
        for (int16_t& kept : unique) {
            if (WiFi.SSID(kept) == ssid) {
                if (WiFi.RSSI(i) > WiFi.RSSI(kept)) {
                    kept = i;
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            unique.push_back(i);
        }
    }

    std::sort(unique.begin(), unique.end(),
              [](int16_t a, int16_t b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

    String out = "[";
    for (size_t i = 0; i < unique.size(); i++) {
        const int16_t n = unique[i];
        if (i) {
            out += ',';
        }
        out += "{\"s\":\"";
        out += jsonEscape(WiFi.SSID(n));
        out += "\",\"r\":";
        out += WiFi.RSSI(n);
        out += ",\"l\":";
        out += (WiFi.encryptionType(n) == WIFI_AUTH_OPEN) ? "0" : "1";
        out += '}';
    }
    out += ']';

    LOGD(kLogTag, "Scan: %d results, %u unique SSIDs", status, unique.size());

    WiFi.scanDelete();
    // Queue the next scan so a rescan tap has fresh results waiting.
    WiFi.scanNetworks(true);

    server.send(200, "application/json", out);
}

void CaptivePortal::handleSave() {
    const String ssid = server.arg("s");
    const String password = server.arg("p");

    if (ssid.isEmpty()) {
        server.send(400, "text/plain", "missing network");
        return;
    }

    server.send(200, "text/plain", "ok");
    LOGI(kLogTag, "Credentials submitted for '%s'", ssid.c_str());

    if (_onSubmit) {
        _onSubmit(ssid, password);
    }
}

void CaptivePortal::handleStatus() {
    server.send(200, "application/json",
                _onStatus ? _onStatus() : String("{\"state\":\"unknown\"}"));
}

void CaptivePortal::handleRedirect() {
    server.sendHeader("Location",
                      "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}
