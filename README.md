# 💾 ESP32 Mini NAS

> **A tiny NAS built from an ESP32, a microSD card, an OLED, and an
> unreasonable amount of enthusiasm.** 😌

ESP32 Mini NAS is a lightweight personal network storage and media
server built around an **ESP32** and a **microSD card**.

The idea started simply: *"I have an ESP32, an OLED, an SD card
module... what happens if I make them do something useful?"*

Several debugging sessions later, we somehow ended up with a miniature
NAS. Humanity remains unpredictable.

------------------------------------------------------------------------

## ✨ What Can It Do?

### 💾 File Storage

-   📁 Browse files and folders
-   📂 Create folders
-   🗑️ Delete files
-   📤 Upload files
-   📥 Download files
-   📊 View file sizes
-   🗂️ Organize files into folders

### 🎬 Built-in Media Viewers

  Type                  Support
  --------------------- -------------
  🖼️ Images             ✅
  🎬 Videos             ✅
  🎵 Audio              ✅
  📄 Text / documents   ✅
  📦 Other files        ⬇️ Download

The ESP32 serves the files over HTTP while the browser handles media
playback. A sensible division of labor, because the ESP32 already has
enough problems.

------------------------------------------------------------------------

## 📤 Upload Progress

The web interface shows: - 📄 Which file is being uploaded - 📊 Upload
percentage - 🚀 Upload speed - ⏱️ Transfer progress

The OLED switches to an upload status display while a transfer is
happening:

``` text
┌──────────────────┐
│    UPLOADING     │
│                  │
│       72%        │
│                  │
│    812 KB/s      │
└──────────────────┘
```

------------------------------------------------------------------------

## 🌡️ DHT11 Environmental Monitoring

A **DHT11 temperature and humidity sensor** is connected to the ESP32.

The readings appear on both the web dashboard and OLED.

``` text
🌡️ Temperature: 29.4 °C
💧 Humidity:    67.0 %
```

The firmware also exposes a `/sensor` endpoint returning JSON:

``` json
{
  "ok": true,
  "temperature": 29.4,
  "humidity": 67.0
}
```

------------------------------------------------------------------------

## 📺 OLED Status Display

The OLED is a real hardware status panel, not just decoration.

Depending on the current operation it can show: - 🌡️ Temperature - 💧
Humidity - 💾 SD-card status - 📊 Storage information - 📤 Upload
progress - 🚀 Transfer speed - ⚠️ Status/error information

------------------------------------------------------------------------

## 🌐 Web Dashboard

The NAS is controlled through a responsive browser interface designed
for: - 🎨 Modern presentation - 📱 Mobile devices - 🖥️ Desktop
browsers - 🔤 Unicode-safe text - 🧭 Simple navigation

It provides access to storage, folders, uploads, files, media viewers,
and sensor information.

------------------------------------------------------------------------

## 🏗️ Hardware

  Component                      Purpose
  ------------------------------ ------------------------------
  🧠 ESP32                       Main controller + web server
  💾 microSD card                File storage
  📺 OLED display                Hardware status display
  🌡️ DHT11                       Temperature + humidity
  🔌 Breadboard + jumper wires   Prototyping

The current development setup uses a **16 GB microSD card**.

------------------------------------------------------------------------

## 🔌 Current Hardware Connections

### DHT11

``` text
DHT11 DATA → GPIO 4
```

### OLED

The OLED communicates with the ESP32 using I²C.

### microSD

The SD-card module uses SPI:

``` text
GND
VCC
MISO
MOSI
SCK
CS
```

> ⚠️ Check the pin definitions in `main.cpp` before rewiring. ESP32
> boards have enough pin-label variations to make this a minor practical
> joke.

------------------------------------------------------------------------

## 🧠 How It Works

``` text
                    ┌─────────────────────┐
                    │      Web Browser    │
                    │  PC / Phone / Tablet│
                    └──────────┬──────────┘
                               │ HTTP
                               ▼
                    ┌─────────────────────┐
                    │        ESP32        │
                    │                     │
                    │   Web Server       │
                    │   File Manager      │
                    │   Media Server      │
                    │   Sensor Manager    │
                    └──────┬───────┬──────┘
                           │       │
                ┌──────────┘       └──────────┐
                ▼                             ▼
        ┌───────────────┐             ┌───────────────┐
        │   microSD     │             │     DHT11     │
        │    Storage    │             │ Temp / Humid. │
        └───────────────┘             └───────────────┘
                           │
                           ▼
                    ┌───────────────┐
                    │      OLED     │
                    │ Status / Info │
                    └───────────────┘
```

------------------------------------------------------------------------

## 🚀 Getting Started

### 1. Clone the repository

``` bash
git clone https://github.com/YOUR_USERNAME/esp32-mini-nas.git
cd esp32-mini-nas
```

### 2. Open with VS Code + PlatformIO

Install: - Visual Studio Code - PlatformIO IDE extension

### 3. Configure Wi-Fi

Update the Wi-Fi configuration in the firmware with your network
credentials.

**Never commit real Wi-Fi passwords to a public repository.**

### 4. Connect the hardware

Connect the ESP32, microSD module, OLED, and DHT11, then insert a
supported microSD card.

### 5. Build and upload

``` bash
pio run
pio run --target upload
```

Or use the PlatformIO Build and Upload buttons in VS Code.

### 6. Open the NAS

Find the ESP32's IP address and open:

``` text
http://ESP32-IP/
```

Congratulations. You now have a tiny NAS. 🎉

------------------------------------------------------------------------

## 📁 Project Structure

``` text
esp32-mini-nas/
│
├── src/
│   └── main.cpp
├── include/
├── lib/
├── test/
├── platformio.ini
├── .gitignore
└── README.md
```

As the project grows, the firmware will be split into separate modules.

Because eventually a several-thousand-line `main.cpp` stops being a file
and starts becoming a cry for help.

------------------------------------------------------------------------

## 🛣️ Roadmap

### 🟢 File Management

-   [x] Browse files
-   [x] Browse folders
-   [x] Create folders
-   [x] Upload files
-   [x] Download files
-   [x] Delete files
-   [ ] 🔍 File search
-   [ ] 🏷️ File type filtering
-   [ ] ✏️ Rename files
-   [ ] ✏️ Rename folders
-   [ ] 📦 Move files and folders
-   [ ] 🗑️ Improved folder deletion

### 🟡 Dashboard

-   [x] Storage information
-   [x] Temperature
-   [x] Humidity
-   [x] Upload progress
-   [x] Upload speed
-   [ ] 📊 Storage analytics
-   [ ] 📈 Transfer statistics
-   [ ] 📡 Wi-Fi signal strength
-   [ ] ⏱️ System uptime
-   [ ] 📋 Recent files

### 🟠 Media

-   [x] 🖼️ Image viewer
-   [x] 🎵 Audio player
-   [x] 🎬 Video player
-   [x] 📄 Text/document viewer
-   [ ] 🖼️ Image thumbnails
-   [ ] 🎵 Improved media metadata
-   [ ] 🎬 Media library
-   [ ] 🔎 Media search

### 🔵 Networking

-   [ ] 🔐 User authentication
-   [ ] 👤 User accounts
-   [ ] 📶 Wi-Fi configuration page
-   [ ] 🌐 Network information
-   [ ] 🔒 Better access control

### 🟣 System

-   [ ] 🔄 OTA firmware updates
-   [ ] ⚙️ Settings page
-   [ ] 🔁 Remote restart
-   [ ] 💾 Persistent configuration
-   [ ] 🧩 Modular firmware architecture
-   [ ] 🩺 System diagnostics

------------------------------------------------------------------------

## ⚡ Performance Reality Check

This is an **ESP32**, not a rack-mounted enterprise NAS.

The goal isn't to compete with Synology or TrueNAS. The goal is to build
something:

-   Small
-   Cheap
-   Educational
-   Portable
-   Hackable
-   Useful

For low-resolution media and reasonable file transfers, the project is
surprisingly capable.

Large files, high-bitrate video, multiple simultaneous users, and heavy
workloads will naturally push the hardware toward its limits.

------------------------------------------------------------------------

## 🔐 Security

The project is intended primarily for use on a **trusted local
network**.

It should **not** be exposed directly to the public internet in its
current development state.

Before doing that, the project needs proper authentication,
authorization, secure credential handling, input validation, safer
file/path handling, and an appropriate secure network setup.

Putting a development NAS on the public internet and hoping nobody
notices is technically a networking strategy, but not a particularly
good one. 😭

------------------------------------------------------------------------

## 🧪 Development Philosophy

The project is being built incrementally:

``` text
Hardware
   ↓
SD card
   ↓
OLED
   ↓
Web server
   ↓
File manager
   ↓
Media server
   ↓
Upload/download progress
   ↓
DHT11 monitoring
   ↓
More NAS features...
```

Each working version becomes a checkpoint before the next feature is
introduced.

------------------------------------------------------------------------

## 🤝 Contributing

This is primarily a personal learning project, but ideas, improvements,
bug reports, and pull requests are welcome.

If you find something broken:

1.  🐛 Open an issue.
2.  📝 Describe what happened.
3.  📷 Include screenshots/logs when useful.
4.  🔧 If you know the fix, a pull request is even better.

Please remember that this runs on an ESP32. If a proposed solution
requires 32 GB of RAM and a dedicated GPU, we may have encountered a
slight architectural disagreement.

------------------------------------------------------------------------

## ❤️ Why This Exists

This started as a pile of components sitting around:

``` text
ESP32
OLED
microSD module
DHT11
breadboard
jumper wires
```

Instead of letting them continue their peaceful retirement on a shelf,
they became a tiny network storage server.

The project is mainly about learning: - Embedded systems - ESP32
development - Networking - HTTP servers - File systems - Web
development - Hardware interfacing - Sensors - Media serving - Software
architecture

And, naturally, learning how many times a single missing semicolon can
ruin an otherwise perfectly good evening.

------------------------------------------------------------------------

## ⭐ Project Status

**🟢 Active Development**

### Current milestone

> **ESP32 Mini NAS --- Stable NAS + Media Server + DHT11 Monitoring**

### Next milestone

> 🔍 **Better file management and a smarter dashboard**

------------------------------------------------------------------------

::: {align="center"}
### 💾 Built with an ESP32, a microSD card, and questionable amounts of ambition.

**ESP32 Mini NAS** 🚀
:::
