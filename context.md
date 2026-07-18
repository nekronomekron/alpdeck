# alpdeck — Projektkontext

Stand: 2026-07-18. Handoff-/Kontextdokument für die Weiterarbeit.

## Überblick

alpdeck ist ein E-Paper-Handheld auf ESP32-S3, das Anwendungen (Apps, Spiele,
Tools) startet. **Alles Ausführbare ist Lua in einer Sandbox** — auch der
Launcher selbst. C++ ist der Kernel (Peripherie, Dateisysteme, Netzwerk,
Lua-Lebenszyklus), Lua ist die App-Nutzlast.

## Hardware

- **Board:** LOLIN S3 PRO (ESP32-S3, 16 MB Flash, PSRAM).
- **Display:** GDEY042T81, 4.2" E-Paper, 400×300, 1-Bit (GxEPD2). Ein einziger
  Framebuffer (15000 Byte, eine Page). Partial-Refresh ~400 ms, Full ~1200 ms.
- **Eingabe:** zwei Controller im I2C-STEMMA-QT-Daisy-Chain, beide optional,
  **mindestens einer muss vorhanden sein** (sonst Boot-Fehler + Halt):
  - Adafruit ANO Rotary Navigation Encoder (seesaw, Produkt **5740**, I2C
    **0x49**): 5-Wege-Switch + Drehrad. Events `rotary_*`.
  - Adafruit Mini I2C Gamepad with seesaw (Produkt **5743**, I2C **0x50**):
    6 Buttons (A/B/X/Y/Start/Select) + Analog-Stick (als Richtungs-Events
    digitalisiert, Hysterese). Events `gamepad_*`.
- **SD-Karte:** teilt den SPI-Bus mit dem Display.

Pin-Quelle ist die Board-Variante `pins_arduino.h` (nicht raten):
- Display: CS=7, DC=8, RST=2, BUSY=1, SCK=12, MISO=13, MOSI=11.
- SD: CS=46 (= `TF_CS` der Variante, bestätigt).
- I2C: SDA=9, SCL=10 (Default der Variante, frei und am Header herausgeführt).
- ANO-Switches sind **seesaw-seitige** Pins, keine ESP32-GPIOs:
  SELECT=1, UP=2, LEFT=3, DOWN=4, RIGHT=5 (active-low, Pull-up).
- Gamepad-Pins (seesaw-seitig, aus Adafruits gamepad_qt-Beispiel):
  SELECT=0, B=1, Y=2, A=5, X=6, START=16; Stick analog X=14, Y=15
  (0–1023, Mitte ~512).

Alle Pins in `src/config/AppConfig.h`.

## Boot-Ablauf

`setup()`:
1. Serial/Logger.
2. `Display::init()` + Bootscreen (bleibt stehen bis der Launcher das erste
   Frame zeichnet — es gibt **kein** `Display::shutdown()` mehr vor den Skripten).
3. LittleFS mounten (`formatOnFail=true`; **fatal** bei Fehlschlag), dann SD
   (nach Display, da gemeinsamer SPI-Bus; optional, nur Warnung). SD-Root wird
   bei LOG_LEVEL 3 geloggt.
4. `Input::init()` — **fatal**, wenn kein Controller gefunden wird
   (Wokwi-Build: nur Warnung, der Simulator hat keine seesaw-Hardware).
5. Netzwerk (nicht-blockierend), FTP startet per Connect-Callback.
6. `LuaHost::init()` (**fatal** bei Allokationsfehler).
7. `boot.lua` läuft (User-Hook). Danach der Launcher, dann die gewählte App.

**Fatal-Pfad:** `bootFail(msg)` in main.cpp — loggt, zeichnet den Bootscreen
neu mit `Bootscreen::drawError()` (Warndreieck mit „!" links neben dem
Fehlertext, unter dem Logo, `\n` erlaubt eine zweite Zeile) und hält das Gerät
in einer yield-Schleife an. Die Progress-Bar-API des Bootscreens ist entfernt.

`loop()`: `Network::loop()`, `DynamicFTPServer::loop()`, `Input::poll()`,
`LuaHost::loop()`.

## Modulkarte (src/core/)

- **Display** — GxEPD2-Wrapper. Immediate-Mode-Frame (`beginFrame`/`canvas`/
  `endFrame`) für die Lua-Bindings, weil Lua aus dem Paged-Loop heraus bei einem
  Fehler per longjmp durch C++-Destruktoren springen könnte. Nur sicher, weil
  das Panel in **eine** Page passt.
- **Vfs** — gemeinsame Pfadauflösung (`Vfs::resolve`): `/sd/...` → SD-Karte,
  alles andere → LittleFS. Einzige Stelle für dieses Mapping; LuaHost und
  LuaBindings nutzen sie (war vorher dupliziert).
- **Logger** (+ `utils/JsonUtil.h`) — thread-sicher: Zeilen werden in einen
  Puffer komponiert und unter Mutex als eine Ausgabe geschrieben, weil Main-Loop
  und Lua-Task gleichzeitig loggen. Interface nimmt `Print&`. `jsonEscape()`
  liegt in `utils/JsonUtil.h` (genutzt von Portal und Network).
- **Network** — nicht-blockierende WiFi-State-Machine. Credentials in
  `Preferences` (NVS). Ohne Credentials → Captive Portal. `WiFi.begin()` kehrt
  sofort zurück, `loop()` pollt (kein Blockieren in `setup()`).
- **CaptivePortal** — eigenes AP-Setup-Portal (kein WiFiManager mehr): Soft-AP,
  Wildcard-DNS, WebServer. Schwarz-weißes UI im Bootscreen-Stil. Dedupliziert
  SSIDs serverseitig (stärkstes BSSID pro Name), Passwortfeld deaktiviert bei
  offenen Netzen.
- **DynamicFTPServer** — LittleFS als `/flash`, SD als `/sd` (kastenklicker/
  ESP-FTP-Server, Multi-Mount). Startet nur bei WiFi-Verbindung. Lebenszyklus
  per `new`/`delete` pro Verbindungszyklus: die Lib hat kein `stop()`, aber
  `~FTPServer` → `~WiFiServer` → `end()` schließt den Socket sauber (das
  frühere `delete &globalObjekt` war UB und crashte beim ersten Disconnect).
- **Input** — Fassade über die Controller-Treiber **RotaryController** (5740)
  und **GamepadController** (5743, Buttons + Stick-Digitalisierung mit
  Hysterese); **SeesawButtons** ist der geteilte Entprell-/Long-Press-Helfer.
  `init()` probt beide, true bei ≥ 1. `poll()` besitzt I2C exklusiv auf dem
  Main-Loop und publiziert Events in eine FreeRTOS-Queue; Lua-Task konsumiert
  per `read()`. Bus wird nie aus zwei Tasks berührt. Event-Namen tragen die
  Quelle (`rotary_up` vs. `gamepad_up`) — Kontrakt für 2-Spieler-Apps. Tasten
  ohne Long-Press feuern beim **Drücken** (Latenz); nur `rotary_select` beim
  Loslassen (Long-Press-Disambiguierung).
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

**Auf Hardware verifiziert (2026-07-18):**
- **Der LUA_32BITS-Fix funktioniert: der Launcher läuft auf dem Gerät und
  zeigt die Hello-App in der Liste an.** Damit sind Display, LittleFS,
  SD-Mount, Lua-Host, App-Discovery und das Launcher-Rendering end-to-end am
  Gerät bestätigt.

**Gebaut und off-device verifiziert:**
- Netzwerk + Captive Portal (UI im Browser gerendert/getestet).
- FTP mit /flash + /sd.
- Robustheits-/Konsistenz-Refactoring (2026-07-17, siehe unten); beide
  Environments (`Alpdeck`, `Alpdeck-Wokwi`) bauen grün.

**Input-Umbau (2026-07-18, Build-verifiziert):**
- Zwei-Controller-Support (Rotary 0x49 + Gamepad 0x50, Daisy-Chain, beide
  optional, mindestens einer Pflicht). Neue Module: `SeesawButtons` (geteiltes
  Entprellen/Long-Press), `RotaryController`, `GamepadController`; `Input` ist
  Fassade mit `hasRotary()`/`hasGamepad()`.
- **Breaking Change im Lua-Kontrakt:** Event-Namen tragen jetzt die Quelle
  (`rotary_cw`, `gamepad_a`, …). launcher.lua, hello-App und der
  API-Kommentar in LuaBindings.h sind angepasst (Lookup-Tabellen statt
  if-Ketten).
- Bootscreen: Progress-API entfernt; `drawError()` (Warndreieck + Text unter
  dem Logo). main.cpp: `bootFail()`-Fatal-Pfad (LittleFS-Mount, kein
  Controller, LuaHost-Init) mit hartem Halt.

**Refactoring-Pass über src/ (2026-07-17):**
- **Bugfix FTP-Shutdown:** `delete &ftpSrv` auf globalem Objekt (UB, Crash beim
  ersten WiFi-Disconnect) → Heap-Lebenszyklus `new`/`delete` in
  DynamicFTPServer.
- **Bugfix Wokwi-Logspam:** Connect-Timeout ohne Portal ließ den State auf
  `Connecting` und loggte „Connect timed out" jeden Loop-Durchlauf → Timer-Reset
  und stiller Retry.
- **Bugfix uint32-Wraparound:** `input.read(-1)`/`sys.delay(-1)` wurden zu ~49
  Tagen Blockade → negative Werte geclampt; `display.text`-Größe geclampt
  (1..8).
- **Bugfix Phantom-Input:** fehlgeschlagener I2C-Read (liefert Nullen) hätte
  active-low als „alle 5 Tasten gedrückt" dekodiert → Sample wird als
  Bus-Glitch verworfen.
- **Härtung:** LuaHost verweigert `run()` bei fehlgeschlagener
  Queue/Mutex-Allokation; `readScript()` prüft auf vollständigen Read (SD-Fehler
  erschien sonst als kryptischer Lua-Syntaxfehler); Logger thread-sicher
  (Mutex + Einzelausgabe); `Network::statusJson()` escaped SSIDs.
- **Struktur:** neues `core/Vfs` (dedupliziertes `resolveFs`), neues
  `utils/JsonUtil.h` (geteiltes `jsonEscape`), einheitliche Includes
  (`"core/X.h"`), Dateiglobale in anonymen Namespaces, Braces/Naming
  vereinheitlicht, toter Code entfernt.

**Zuvor behobene Bugs (chronologisch):**
- boot.lua unsichtbar über FTP → falscher STORAGE_TYPE / uploadfs nötig.
- Display-Pin-GPIO-Warnungen (kosmetisch, gefixt).
- Portal-Crash → Loop-Stack + nicht-blockierendes Portal (WiFiManager ersetzt).
- Launcher: `math` nil → `luaL_requiref` statt bare `luaopen_*`.
- Launcher: Blank-Screen (`show(full)` rendert leeres Frame) + Selektion
  schwarz-auf-schwarz (invert-Flag).
- Launcher: keine Apps gefunden → Lua-Integer-ABI-Mismatch (LUA_32BITS) —
  **am Gerät bestätigt behoben.**

## Offene Punkte

1. **Encoder wird nicht erkannt:** zuletzt `[WARN][Input] No encoder at 0x49`.
   Prüfen: I2C-Verkabelung (SDA=9, SCL=10, 3V3, GND), STEMMA-Adresse. Die
   Treiber prüfen die Produkt-ID zur Laufzeit und degradieren sauber. **Achtung:
   ohne irgendeinen Controller stoppt der Boot jetzt absichtlich im
   Fehler-Screen.**
2. **Input-Umbau (2 Controller, rotary_*/gamepad_*-Events) lief noch nicht auf
   Hardware.** Zu verifizieren: Gamepad-Erkennung an 0x50,
   Joystick-Achsen-Orientierung (`GAMEPAD_STICK_INVERT_X/Y` in AppConfig ggf.
   drehen), Schwellen (`GAMEPAD_STICK_PRESS/RELEASE`), Fehler-Screen ohne
   Controller. launcher.lua/hello-App brauchen `uploadfs` bzw. SD-Update, da
   die alten Event-Namen (`cw`, `up`, …) nicht mehr gesendet werden.
3. **Refactoring-Pass lief noch nicht auf Hardware** (nur Build-verifiziert).
   Besonders der neue FTP-Lebenszyklus (Disconnect → Shutdown → Reconnect)
   ist am Gerät noch ungetestet.
4. **32-Bit-Integer/Float in Lua** (durch LUA_32BITS) — für Launcher/Apps
   ausreichend, aber ein bewusster Trade-off gegenüber 64-Bit/double.
5. Alte committete WiFi-Credentials (`IoT`/`05021904`) liegen noch in der
   Git-History (Commit fc1434d) — bei öffentlichem Repo rotieren.
6. Kein `require` (Single-File-Apps) — relevant, falls Apps größer werden.
