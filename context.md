# alpdeck — Projektkontext

Stand: 2026-07-17. Handoff-/Kontextdokument für die Weiterarbeit.

## Überblick

alpdeck ist ein E-Paper-Handheld auf ESP32-S3, das Anwendungen (Apps, Spiele,
Tools) startet. **Alles Ausführbare ist Lua in einer Sandbox** — auch der
Launcher selbst. C++ ist der Kernel (Peripherie, Dateisysteme, Netzwerk,
Lua-Lebenszyklus), Lua ist die App-Nutzlast.

## Hardware

- **Board:** LOLIN S3 PRO (ESP32-S3, 16 MB Flash, PSRAM).
- **Display:** GDEY042T81, 4.2" E-Paper, 400×300, 1-Bit (GxEPD2). Ein einziger
  Framebuffer (15000 Byte, eine Page). Partial-Refresh ~400 ms, Full ~1200 ms.
- **Eingabe:** Adafruit ANO Rotary Navigation Encoder über I2C STEMMA QT
  (seesaw, Produkt **5740**, I2C-Adresse **0x49**). 5-Wege-Switch + Drehrad.
- **SD-Karte:** teilt den SPI-Bus mit dem Display.

Pin-Quelle ist die Board-Variante `pins_arduino.h` (nicht raten):
- Display: CS=7, DC=8, RST=2, BUSY=1, SCK=12, MISO=13, MOSI=11.
- SD: CS=46 (= `TF_CS` der Variante, bestätigt).
- I2C: SDA=9, SCL=10 (Default der Variante, frei und am Header herausgeführt).
- ANO-Switches sind **seesaw-seitige** Pins, keine ESP32-GPIOs:
  SELECT=1, UP=2, LEFT=3, DOWN=4, RIGHT=5 (active-low, Pull-up).

Alle Pins in `src/config/AppConfig.h`.

## Boot-Ablauf

`setup()`:
1. Serial/Logger.
2. `Display::init()` + Bootscreen (bleibt stehen bis der Launcher das erste
   Frame zeichnet — es gibt **kein** `Display::shutdown()` mehr vor den Skripten).
3. LittleFS mounten (`formatOnFail=true`), dann SD (nach Display, da gemeinsamer
   SPI-Bus). SD-Root wird bei LOG_LEVEL 3 geloggt.
4. Netzwerk (nicht-blockierend), FTP startet per Connect-Callback.
5. `Input::init()`, `LuaHost::init()`.
6. `boot.lua` läuft (User-Hook). Danach der Launcher, dann die gewählte App.

`loop()`: `Network::loop()`, `DynamicFTPServer::loop()`, `Input::poll()`,
`LuaHost::loop()`.

## Modulkarte (src/core/)

- **Display** — GxEPD2-Wrapper. Immediate-Mode-Frame (`beginFrame`/`canvas`/
  `endFrame`) für die Lua-Bindings, weil Lua aus dem Paged-Loop heraus bei einem
  Fehler per longjmp durch C++-Destruktoren springen könnte. Nur sicher, weil
  das Panel in **eine** Page passt.
- **Network** — nicht-blockierende WiFi-State-Machine. Credentials in
  `Preferences` (NVS). Ohne Credentials → Captive Portal. `WiFi.begin()` kehrt
  sofort zurück, `loop()` pollt (kein Blockieren in `setup()`).
- **CaptivePortal** — eigenes AP-Setup-Portal (kein WiFiManager mehr): Soft-AP,
  Wildcard-DNS, WebServer. Schwarz-weißes UI im Bootscreen-Stil. Dedupliziert
  SSIDs serverseitig (stärkstes BSSID pro Name), Passwortfeld deaktiviert bei
  offenen Netzen.
- **DynamicFTPServer** — LittleFS als `/flash`, SD als `/sd` (kastenklicker/
  ESP-FTP-Server, Multi-Mount). Startet nur bei WiFi-Verbindung.
- **Input** — ANO-Encoder. `poll()` besitzt I2C exklusiv auf dem Main-Loop und
  publiziert Events in eine FreeRTOS-Queue; Lua-Task konsumiert per `read()`.
  Bus wird nie aus zwei Tasks berührt.
- **LuaHost** — Task-Supervisor. Genau **ein** `lua_State`/Task gleichzeitig.
  App fordert nächste App per `sys.launch(path)` an und *returnt*; der Host baut
  den State ab und startet dann die nächste. Ein Crash landet wieder beim
  Launcher.
- **LuaBindings** — die öffentliche API für Apps: `display`, `input`, `fs`
  (pfad-sandboxed), `sys`. Basis-Lib ohne `io`/`os`/`package`, also kein
  `require` → eine App = eine Datei.
- **LuaWrapper** (lib/luawrapper) — pro Launch instanziiert. PSRAM-Allokator,
  Traceback-Handler, kooperativer Cancel-Hook (`lua_sethook`, **nie**
  `vTaskDelete` — würde den SPI-Mutex stranden), `lua_atpanic` (sonst rebootet
  ein API-Fehler das Gerät).

## App-Format

- `/sd/apps/<name>/main.lua` (Pflicht) + optional `app.lua` (Manifest, gibt eine
  Lua-Tabelle zurück: `return { name=..., version=... }` — kein JSON, da der
  Interpreter schon da ist).
- **Wichtig:** `/sd` ist ein virtueller Mount; auf der Karte liegt die App
  physisch unter `/apps/<name>/`. Beispiel-App in `sdcard/apps/hello/` — der
  *Inhalt* von `sdcard/` gehört ins Wurzelverzeichnis der Karte.
- Der Launcher erkennt eine App an einer vorhandenen `main.lua` (nicht am
  Verzeichnis-Flag).

## Drei tragende Invarianten (nicht brechen)

1. **Genau ein `lua_State` gleichzeitig.** Nie geschachtelt starten.
2. **Cancellation ist kooperativ** (`lua_sethook`). Nie `vTaskDelete` auf einen
   Lua-Task — er könnte den gemeinsamen SPI-Mutex halten und Display + SD für
   das ganze Gerät blockieren.
3. **Kein `io`/`os`/`package`.** `fs.*` ist der einzige Speicherzugriff, jeder
   Pfad wird geprüft.

## Nicht offensichtliche Fallen (in Memory dokumentiert)

- **seesaw ist vendored** (`lib/seesaw`), nicht aus der Registry. Das
  Registry-Paket zieht ST7735 → `arduino-libraries/SD`, das die SD-Lib des
  ESP32-Cores überschattet und den Build bricht. `lib_ignore = SD` hilft nicht
  (beide heißen `SD`). → nicht "reparieren" durch Re-Hinzufügen.
- **Lua-Integer-Breite per globalem Build-Flag** (`-DLUA_32BITS`), nie per
  Header-Define. Ein `#define LUA_USE_C89` im Header setzt `LUA_INTEGER` auf
  32-Bit — aber nur für Dateien, die den Header einbinden, während die Lua-`.c`
  bei 64-Bit bleiben. Folge: ABI-Mismatch, jeder Integer über die C-Grenze wird
  Müll (Tabellenschlüssel, Größen). War der "keine Apps gefunden"-Bug.
  `static_assert(sizeof(lua_Integer)==4)` sichert das Flag ab.
- **Off-Device-Test mit lupa** öffnet die *volle* Stdlib; das Gerät nur
  base/table/string/math. Immer mit restringiertem `_ENV` testen, sonst laufen
  Skripte durch, die auf Hardware scheitern (so kam der `math`-nil-Bug durch).
  lupa kann C-seitige Binding-Bugs prinzipiell nicht fangen (es hat eigene Stdlib).

## Build & Flash

```
pio run -e Alpdeck                 # Firmware bauen
pio run -e Alpdeck -t upload       # Firmware flashen (C++-Änderungen)
pio run -e Alpdeck -t uploadfs     # LittleFS flashen (boot.lua, launcher.lua)
pio run -e Alpdeck -t buildfs      # nur FS-Image bauen
pio run -e Alpdeck -t factory      # ein einzelnes factory.bin (Bootloader +
                                   # Firmware + LittleFS) für Endnutzer, flashbar
                                   # an 0x0. Script: scripts/factory_image.py
```

Zwei Environments: `Alpdeck` (Hardware) und `Alpdeck-Wokwi` (Simulator; Portal
wird auskompiliert, joint direkt `Wokwi-GUEST`). pio-Pfad hier:
`~/.platformio/penv/Scripts/pio.exe`.

**Merke:** `upload` flasht nie das Dateisystem. `boot.lua`/`launcher.lua`
brauchen immer separat `uploadfs`. Nach einem Flash-Erase ist LittleFS leer.

## Verifikations-Werkzeuge (off-device, in %TEMP%)

- `sandbox_check.py` — führt alle Lua-Skripte unter dem echten Sandbox-`_ENV`
  aus (lupa), prüft dass `os`/`io`/`require` blockiert sind, plus Regressionen
  (math/string/table vorhanden, selektierte Zeile invertiert lesbar).
- `render_launcher.py` — rendert den Launcher durch einen GxEPD2-treuen Mock
  (inkl. `firstPage()`-whitens-Verhalten) zu PNG; zählt schwarze Pixel gegen den
  Blank-Screen-Bug.
- `dir_flag_test.py` — beweist, dass Apps unabhängig vom `dir`-Flag gefunden werden.

## Aktueller Stand

**Gebaut und off-device verifiziert:**
- Display, LittleFS, SD-Mount laufen (per Serial-Log bestätigt).
- Netzwerk + Captive Portal (UI im Browser gerendert/getestet).
- FTP mit /flash + /sd.
- Launcher-Logik, App-Discovery, invertierte Selektion, Blank-Screen-Fix.
- Lua-Integer-ABI-Fix (`LUA_32BITS`), Build grün, static_assert hält.

**Zuletzt behobene Bugs (chronologisch):**
- boot.lua unsichtbar über FTP → falscher STORAGE_TYPE / uploadfs nötig.
- Display-Pin-GPIO-Warnungen (kosmetisch, gefixt).
- Portal-Crash → Loop-Stack + nicht-blockierendes Portal (WiFiManager ersetzt).
- Launcher: `math` nil → `luaL_requiref` statt bare `luaopen_*`.
- Launcher: Blank-Screen (`show(full)` rendert leeres Frame) + Selektion
  schwarz-auf-schwarz (invert-Flag).
- **Launcher: keine Apps gefunden → Lua-Integer-ABI-Mismatch (LUA_32BITS).**

## Offene Punkte

1. **Encoder wird nicht erkannt:** `[WARN][Input] No encoder at 0x49`. Nächster
   Blocker für echte Navigation. Prüfen: I2C-Verkabelung (SDA=9, SCL=10, 3V3,
   GND), STEMMA-Adresse. `Input::init()` prüft die Produkt-ID (5740) zur Laufzeit
   und degradiert sauber statt zu hängen.
2. **Nichts davon lief je auf Hardware verifiziert** über den Boot-Log hinaus.
   Der aktuelle Fix (LUA_32BITS) ist logisch wasserdicht, aber noch nicht am
   Gerät bestätigt — erwartet beim nächsten Boot `discover: 1 app(s) found` und
   "Hello" in der Liste.
3. **32-Bit-Integer/Float in Lua** (durch LUA_32BITS) — für Launcher/Apps
   ausreichend, aber ein bewusster Trade-off gegenüber 64-Bit/double.
4. Alte committete WiFi-Credentials (`IoT`/`05021904`) liegen noch in der
   Git-History (Commit fc1434d) — bei öffentlichem Repo rotieren.
5. Kein `require` (Single-File-Apps) — relevant, falls Apps größer werden.
