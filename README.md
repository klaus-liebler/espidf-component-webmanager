# WebManager for ESP IDF

## Features (End User Perspective)
* Wifi Manager
* Timeseries with smart storage in flash
* Configuration / User Settings Manager
* Secure Connection
* Live Log
* Stored Journal
* Integrates with my home automation solution "sensact"

## Plugins (opt-in)

Plugins implement `webmanager::iWebmanagerPlugin` and are passed to `Begin()` in the plugin vector.
Ready-made ones in `cpp/webmanager_plugins/`:

* `SystemInfoPlugin` (namespace `systeminfo`): partitions, chip, MACs, restart.
* `UsersettingsPlugin` (namespace `usersettings`).
* `FirmwareLogPlugin` (namespace `firmwarelog`, schema `best_binary_buffers_schema/firmwarelog.cs`): the
  ESP-IDF log (ESP_LOGx) in the browser. Create it as early as possible in `app_main` so that it buffers the
  boot messages too (`new FirmwareLogPlugin(capacity)`, about 225 bytes per line, in PSRAM if available).
  A hook on `esp_log_set_vprintf` passes every line on to the previous output (UART) unchanged and stores it
  parsed (level, tag, text) in a ring buffer; an own task sends after the browser asked for the history
  (`RequestLogHistory`), first the buffer, then every new line. `RequestSetLogLevel` calls
  `esp_log_level_set`. In the web UI: `AppController.AddTerminal()` of `@klaus-liebler/web-components`.

## Features (Library User)

* vite-based build process
* Web Application is <30k thanks to severe uglifying and brotli compressing
* To Do before Build
  * Connect a ESP32 board ( an individual MAC adress)
  * File builder/gulpfile_config.ts --> adjust paths and ports and names
  * File usersettings/usersettings.ts --> define all settings, that you need in your application
  * File webui_htmlscss/*.scss --> change styles
  * File webui_htmlscss/app.html --> add/delete further screens in the nav-Element and as main element
  * Optional: Project Description Files from my Home Automation Project "Sensact". If you do not need it, check File webui_htmlscss/app.html --> Delete the nav-Element. And delete appropriate main Element. Delete the ScreenController call in webui_logic/app.ts -->Init function
* What happens then
  * Node gets MAC-Adress from ESP32 Chip and builds cetificates for HTTPS

## Features (Developer Perspective)
* Complex Build Process demystified
* HTML Full Screen UI using the (relatively new) techniques "grid", "template", "dialog"

## Build

### When you use this library for the first time -> Preparation 
1. Make sure, there is a system environment variable IDF_PATH
1. install typescript globally call `npm install -g typescript`
2. install gulp-cli globally: call `npm install -g gulp-cli`
3. Open ESP_IDF Terminal (Press F1 and search for "Terminal")
4. to install all javascript / typescript libraries for web runtime: in `components\webmanager\web` call `npm i`
5. to install all javascript / typescript libraries for testserver: in `components\webmanager\testserver` call `npm i`
6. to install all javascript / typescript libraries for build system: in `components\webmanager\builder` call `npm i`
7. to create your own root certificate+private key rootCA.pem.crt+rootCA.pem.privkey: In `components\webmanager\builder` call `gulp rootCA` 
8. Install rootCA certificate in Windows
  - right click on file rootCA.cert.crt
  - choose "Install Certificate"
  - choose Local Computer
  - Select "Alle Zertifikate in folgendem Speicher speichern"
  - Click "Durchsuchen"
  - Select "Vertrauenswürdige Stammzertifizierungsstellen"
  - Click "Next" or "Finish"
### When you start a new project that uses this library -> Project Configuration
1. Edit `builder/gulpfile_config.ts` 
2. Open Menuconfig in esp-idf and set:
     * Max HTTP Request Header Length ->1024
     * CONFIG_ESP_HTTPS_SERVER_ENABLE
     * CONFIG_HTTPD_WS_SUPPORT
     * Partition Table = Custom Partition Table
     * Flash Size >=8MB
     * Detect Flash Size when flashing bootloader
     * CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=3584

### When you intend to flash to a new ESP32 microcontroller (with a specific mac adress)
1. In `components\webmanager\builder` call `gulp gethostname`. This fetches the mac address of a connected ESP32 board and creates a valid hostname out of it based on the hostname template found in `builder/gulpfile_config.ts`. The hostname is writte into file `certificated/esp32_hostname.txt`
  -Alternative: Write the desired hostname of the esp32 board manually in the file `certificated/esp32_hostname.txt`
2. In `components\webmanager\builder` call `gulp certificates`. This creates HTTPS certificates for the ESP32 microcontroller and for the testserver on the local pc and for my public webserver on my university

### When you want to define the available usersettings (Name, Type, default Value), that can be setup in webui
1. Edit `usersettings/go_here/go_here/usersettings.ts` (The "go_here/go_here"- thing is necessary to have the same relative paths here as later in the project. This allows IntelliSense while editing this file :-)

### When you want to build the project
1. In `components\webmanager\builder` call and call `gulp`. The process is quite complex and - unfortunately error prone. Description below:

2. In `components\webmanager\web` and call `npm run build`
3. "Build, Flash and Start a Monitor" esp-idf project

### When you want to flash the initial nvs partion values (usersettings) to the nvs partition
1. Precondition: Project including the custom partition table should have been flashed to the ESP32
1. In `components\webmanager\builder` call `gulp flashusersettings`. This (re)sets the nvs partition to contain an initial value for all usersettings (problem: it resets ALL value. Hence, when you already did some changes for example on the wifi password, these changes get lost)

##Whats happening during `gulp` build?
1. Delete all previously generated files
2. Usersettings:
  1. Generate a csv-File, that contains the default values for all usersettings. The CSV-File fulfils the format of the Espressif partition generator tool
  1. The Espressif partition generator tool is called an a usersettings_partition.bin file (ready to flash) is created.
  1. Generate c++ accessor code for all usersettings (no template; all code is generated here)
  1. The go_here/go_here/usersettings.ts file is copied to the correct location
3. Sencact (only, if sensact option is active, see `builder/gulpfile_usersettings.ts`. If not active, then default empty files are used)
  1. Fetch the generated flatbuffer files from sensact configuration tool
  2. Fill a template `sendCommand` to be able to create all known sensact commands in Typescript, distribute the file to the web project
  3. Fill a template `sensactapps` with Contructors for all apps so that the can create their html, distribute to the web project
4. Flatbuffers
  1. Create Flatbuffer C++ code from all flatbuffer source files (static files and dynamic files from step 3.1)
  2. Create Flatbuffer Typescript code from all flatbuffer source files
  3. Copy Flatbuffer Typescript code to the web project as well as to the testserver project, as both need the code.


## Deprecated / Snippets /Old

Um Sensact in der WebUI zu aktivieren/deaktivieren:
- webui_logic/app.ts ->passend ein- und auskommentieren in der startup()-Methode
- builder/gulpfile_config.ts ->OPTION_SENSACT o.ä passend setzen
- flatbuffers/app.fbs -> includes passen ein- und auskommentieren und im "union Message" ebenfalls passend ein- und auskommentieren
- Hinweis: Keine Veränderungen im HTML/SCSS-Bereich - dort bleibt derzeit immer alles "drin"

