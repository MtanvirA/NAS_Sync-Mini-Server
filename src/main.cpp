#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>

#include <SPI.h>
#include <SD.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ============================================================
// Wi-Fi
// ============================================================

const char *WIFI_SSID = "Octa'Sync";
const char *WIFI_PASSWORD = "2011422918";

// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1);

// ============================================================
// DHT11 temperature / humidity sensor
// ============================================================

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

float currentTemperature = NAN;
float currentHumidity = NAN;
unsigned long lastDHTRead = 0;

const unsigned long DHT_READ_INTERVAL = 2000;

// Read the DHT11 only every 2 seconds. This keeps the sensor happy
// and gives the web dashboard/OLED a stable cached value.
void updateDHT()
{
  unsigned long now = millis();

  if (lastDHTRead != 0 &&
      now - lastDHTRead < DHT_READ_INTERVAL)
  {
    return;
  }

  lastDHTRead = now;

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (!isnan(humidity) && !isnan(temperature))
  {
    currentHumidity = humidity;
    currentTemperature = temperature;
  }
}

// Forward declaration. The function is defined later in the file,
// but the upload handler uses it when an upload is cancelled.
void refreshNormalOLED();

// ============================================================
// SD CARD
// ============================================================

#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

SPIClass spi = SPIClass(VSPI);

// ============================================================
// Web server
// ============================================================

WebServer server(80);
unsigned long lastOLEDRefresh = 0;

const unsigned long OLED_REFRESH_INTERVAL = 10000;

// ============================================================
// Upload state
// ============================================================

File uploadFile;
String uploadPath;
bool uploadInProgress = false;

uint64_t uploadBytesReceived = 0;
uint64_t uploadTotalBytes = 0;
unsigned long uploadStartMillis = 0;
unsigned long uploadLastUpdateMillis = 0;
uint64_t uploadLastBytes = 0;
double uploadSpeedBps = 0.0;

// ============================================================
// OLED
// ============================================================

void showOLED(
    String line1,
    String line2 = "",
    String line3 = "",
    String line4 = "")
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(line1);

  display.setCursor(0, 16);
  display.println(line2);

  display.setCursor(0, 32);
  display.println(line3);

  display.setCursor(0, 48);
  display.println(line4);

  display.display();
}

// ============================================================
// Upload OLED
// ============================================================

String formatSpeed(double bytesPerSecond)
{
  if (bytesPerSecond < 1024.0)
    return String(bytesPerSecond, 0) + " B/s";

  if (bytesPerSecond < 1024.0 * 1024.0)
    return String(bytesPerSecond / 1024.0, 1) + " KB/s";

  return String(bytesPerSecond / 1024.0 / 1024.0, 2) + " MB/s";
}

void showUploadOLED()
{
  int percent = 0;

  if (uploadTotalBytes > 0)
  {
    percent =
        (int)(((double)uploadBytesReceived /
               (double)uploadTotalBytes) *
              100.0);

    if (percent > 100)
      percent = 100;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 8);
  display.print("UPLOAD: ");
  display.print(percent);
  display.println("%");

  display.setCursor(0, 32);
  display.print("SPEED: ");
  display.println(formatSpeed(uploadSpeedBps));

  display.display();
}

// ============================================================
// HTML escaping
// ============================================================

String htmlEscape(const String &input)
{
  String output;
  output.reserve(input.length() + 20);

  for (size_t i = 0; i < input.length(); i++)
  {
    switch (input[i])
    {
    case '&':
      output += "&amp;";
      break;

    case '<':
      output += "&lt;";
      break;

    case '>':
      output += "&gt;";
      break;

    case '"':
      output += "&quot;";
      break;

    case '\'':
      output += "&#39;";
      break;

    default:
      output += input[i];
      break;
    }
  }

  return output;
}

// ============================================================
// URL encode
// ============================================================

String urlEncode(const String &input)
{
  const char *hex = "0123456789ABCDEF";

  String output;

  for (size_t i = 0; i < input.length(); i++)
  {
    unsigned char c = input[i];

    if (
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' ||
        c == '_' ||
        c == '.' ||
        c == '~' ||
        c == '/')
    {
      output += char(c);
    }
    else
    {
      output += '%';
      output += hex[(c >> 4) & 0x0F];
      output += hex[c & 0x0F];
    }
  }

  return output;
}

// ============================================================
// Clean / normalize SD path
// ============================================================

String cleanPath(String path)
{
  path.replace('\\', '/');

  while (path.indexOf("//") >= 0)
  {
    path.replace("//", "/");
  }

  if (!path.startsWith("/"))
  {
    path = "/" + path;
  }

  if (path.indexOf("..") >= 0)
  {
    return "";
  }

  return path;
}

// ============================================================
// Get filename from path
// ============================================================

String getFilename(const String &path)
{
  int slash = path.lastIndexOf('/');

  if (slash < 0)
  {
    return path;
  }

  return path.substring(slash + 1);
}

// ============================================================
// Get parent directory
// ============================================================

String getParentPath(String path)
{
  path = cleanPath(path);

  if (path == "" || path == "/")
  {
    return "/";
  }

  int slash = path.lastIndexOf('/');

  if (slash <= 0)
  {
    return "/";
  }

  return path.substring(0, slash);
}

// ============================================================
// Human-readable size
// ============================================================

String formatBytes(uint64_t bytes)
{
  if (bytes < 1024)
  {
    return String(bytes) + " B";
  }

  if (bytes < 1024ULL * 1024ULL)
  {
    return String(
               (double)bytes / 1024.0,
               1) +
           " KB";
  }

  if (bytes < 1024ULL * 1024ULL * 1024ULL)
  {
    return String(
               (double)bytes / 1024.0 / 1024.0,
               1) +
           " MB";
  }

  return String(
             (double)bytes /
                 1024.0 /
                 1024.0 /
                 1024.0,
             2) +
         " GB";
}

// ============================================================
// File extension
// ============================================================

String getExtension(String filename)
{
  int dot = filename.lastIndexOf('.');

  if (dot < 0)
  {
    return "";
  }

  String ext =
      filename.substring(dot + 1);

  ext.toLowerCase();

  return ext;
}

// ============================================================
// File type
// ============================================================

bool isImage(String name)
{
  String ext = getExtension(name);

  return (
      ext == "jpg" ||
      ext == "jpeg" ||
      ext == "png" ||
      ext == "gif" ||
      ext == "webp" ||
      ext == "bmp");
}

bool isVideo(String name)
{
  String ext = getExtension(name);

  return (
      ext == "mp4" ||
      ext == "webm" ||
      ext == "mov" ||
      ext == "m4v");
}

bool isAudio(String name)
{
  String ext = getExtension(name);

  return (
      ext == "mp3" ||
      ext == "wav" ||
      ext == "ogg" ||
      ext == "m4a" ||
      ext == "aac");
}

bool isText(String name)
{
  String ext = getExtension(name);

  return (
      ext == "txt" ||
      ext == "log" ||
      ext == "csv" ||
      ext == "json" ||
      ext == "xml" ||
      ext == "html" ||
      ext == "css" ||
      ext == "js" ||
      ext == "cpp" ||
      ext == "c" ||
      ext == "h" ||
      ext == "hpp" ||
      ext == "py" ||
      ext == "java" ||
      ext == "md");
}

// ============================================================
// MIME type
// ============================================================

String getMimeType(String filename)
{
  String ext = getExtension(filename);

  if (ext == "jpg" || ext == "jpeg")
    return "image/jpeg";

  if (ext == "png")
    return "image/png";

  if (ext == "gif")
    return "image/gif";

  if (ext == "webp")
    return "image/webp";

  if (ext == "bmp")
    return "image/bmp";

  if (ext == "mp4" || ext == "m4v")
    return "video/mp4";

  if (ext == "webm")
    return "video/webm";

  if (ext == "mov")
    return "video/quicktime";

  if (ext == "mp3")
    return "audio/mpeg";

  if (ext == "wav")
    return "audio/wav";

  if (ext == "ogg")
    return "audio/ogg";

  if (ext == "m4a")
    return "audio/mp4";

  if (ext == "aac")
    return "audio/aac";

  if (ext == "pdf")
    return "application/pdf";

  if (ext == "json")
    return "application/json; charset=utf-8";

  if (ext == "txt" ||
      ext == "log" ||
      ext == "csv" ||
      ext == "md")
    return "text/plain; charset=utf-8";

  if (ext == "html")
    return "text/html; charset=utf-8";

  if (ext == "css")
    return "text/css; charset=utf-8";

  if (ext == "js")
    return "text/javascript; charset=utf-8";

  return "application/octet-stream";
}

// ============================================================
// File icon
// ============================================================

String getFileIcon(String name)
{
  if (isImage(name))
    return "🖼️";

  if (isVideo(name))
    return "🎬";

  if (isAudio(name))
    return "🎵";

  if (isText(name))
    return "📄";

  String ext = getExtension(name);

  if (ext == "pdf")
    return "📕";

  if (
      ext == "zip" ||
      ext == "rar" ||
      ext == "7z")
    return "📦";

  return "📄";
}

// ============================================================
// Storage percentage
// ============================================================

int storagePercentage()
{
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();

  if (total == 0)
    return 0;

  return (int)(((double)used / total) * 100.0);
}

// ============================================================
// Main CSS
// ============================================================

String css()
{
  return R"rawliteral(

<style>

* {
    box-sizing: border-box;
}

:root {
    --bg: #080b12;
    --surface: #111722;
    --surface2: #171f2d;
    --surface3: #1d2636;

    --border: #293449;

    --text: #f2f5fa;
    --muted: #8f9bad;

    --accent: #6d8cff;
    --accent2: #936cff;

    --green: #39d49b;
    --red: #ff6378;

    --shadow:
        0 20px 60px rgba(0,0,0,.25);
}

body {
    margin: 0;

    min-height: 100vh;

    background:
        radial-gradient(
            circle at 0% 0%,
            rgba(109,140,255,.13),
            transparent 30%
        ),
        radial-gradient(
            circle at 100% 0%,
            rgba(147,108,255,.10),
            transparent 28%
        ),
        var(--bg);

    color: var(--text);

    font-family:
        Inter,
        -apple-system,
        BlinkMacSystemFont,
        "Segoe UI",
        Roboto,
        Helvetica,
        Arial,
        sans-serif;

    -webkit-font-smoothing: antialiased;
}

a {
    color: inherit;
}

.container {
    width: min(1050px, calc(100% - 28px));

    margin: auto;

    padding:
        28px 0 60px;
}

.header {
    display: flex;

    justify-content: space-between;

    align-items: center;

    gap: 15px;

    margin-bottom: 24px;
}

.brand {
    display: flex;

    align-items: center;

    gap: 13px;
}

.logo {
    width: 48px;
    height: 48px;

    display: grid;

    place-items: center;

    border-radius: 14px;

    background:
        linear-gradient(
            135deg,
            var(--accent),
            var(--accent2)
        );

    font-size: 23px;

    box-shadow:
        0 10px 35px
        rgba(109,140,255,.25);
}

h1 {
    margin: 0;

    font-size: 23px;

    letter-spacing: -.5px;
}

.subtitle {
    margin-top: 3px;

    color: var(--muted);

    font-size: 13px;
}

.status {
    display: flex;

    align-items: center;

    gap: 8px;

    padding:
        8px 12px;

    border-radius: 999px;

    background:
        rgba(57,212,155,.08);

    border:
        1px solid
        rgba(57,212,155,.18);

    color: var(--green);

    font-size: 12px;

    font-weight: 600;
}

.status-dot {
    width: 8px;
    height: 8px;

    border-radius: 50%;

    background: var(--green);

    box-shadow:
        0 0 12px
        rgba(57,212,155,.8);
}

.card {
    background:
        linear-gradient(
            145deg,
            rgba(255,255,255,.035),
            rgba(255,255,255,.012)
        ),
        var(--surface);

    border:
        1px solid var(--border);

    border-radius: 19px;

    padding: 21px;

    margin-bottom: 18px;

    box-shadow: var(--shadow);
}

.section-head {
    display: flex;

    justify-content: space-between;

    align-items: center;

    gap: 10px;

    margin-bottom: 16px;
}

.section-head h2 {
    margin: 0;

    font-size: 17px;
}

.muted {
    color: var(--muted);
}

.storage-head {
    display: flex;

    justify-content: space-between;

    align-items: flex-start;
}

.storage-percent {
    font-size: 21px;

    font-weight: 700;
}

.progress {
    height: 9px;

    margin:
        17px 0;

    overflow: hidden;

    border-radius: 999px;

    background: #232d40;
}

.progress-bar {
    height: 100%;

    background:
        linear-gradient(
            90deg,
            var(--accent),
            var(--accent2)
        );

    border-radius: inherit;
}

.stats {
    display: grid;

    grid-template-columns:
        repeat(3, 1fr);

    gap: 10px;
}

.stat {
    background: var(--surface2);

    border:
        1px solid var(--border);

    border-radius: 11px;

    padding: 12px;
}

.stat-label {
    color: var(--muted);

    font-size: 11px;

    margin-bottom: 5px;
}

.stat-value {
    font-size: 14px;

    font-weight: 600;
}

.breadcrumbs {
    display: flex;

    flex-wrap: wrap;

    align-items: center;

    gap: 5px;

    margin-bottom: 17px;

    font-size: 13px;
}

.breadcrumbs a {
    color: #b8c6ff;

    text-decoration: none;
}

.breadcrumbs a:hover {
    text-decoration: underline;
}

.breadcrumb-separator {
    color: #58647a;
}

.file-list {
    display: flex;

    flex-direction: column;

    gap: 8px;
}

.file {
    display: flex;

    align-items: center;

    gap: 13px;

    padding: 12px;

    background: var(--surface2);

    border:
        1px solid var(--border);

    border-radius: 13px;

    transition:
        .15s ease;
}

.file:hover {
    background: var(--surface3);

    border-color: #3a4860;

    transform: translateY(-1px);
}

.file-icon {
    width: 42px;
    height: 42px;

    display: grid;

    place-items: center;

    flex: 0 0 42px;

    border-radius: 11px;

    background:
        rgba(109,140,255,.09);

    font-size: 20px;
}

.file-info {
    min-width: 0;

    flex: 1;
}

.file-name {
    font-size: 14px;

    font-weight: 600;

    overflow-wrap: anywhere;
}

.file-meta {
    margin-top: 4px;

    color: var(--muted);

    font-size: 11px;
}

.file-actions {
    display: flex;

    gap: 6px;

    flex-shrink: 0;
}

.btn {
    display: inline-flex;

    align-items: center;

    justify-content: center;

    gap: 5px;

    padding:
        8px 11px;

    border-radius: 9px;

    border:
        1px solid var(--border);

    background: #1c2535;

    color: var(--text);

    text-decoration: none;

    font-size: 12px;

    font-weight: 600;

    cursor: pointer;
}

.btn:hover {
    background: #253047;
}

.btn-primary {
    background:
        linear-gradient(
            135deg,
            var(--accent),
            var(--accent2)
        );

    border: 0;

    color: white;
}

.btn-danger {
    color: #ff9aaa;
}

.folder {
    background:
        rgba(109,140,255,.045);
}

.folder .file-icon {
    background:
        rgba(109,140,255,.13);
}

.upload {
    border:
        1px dashed #3b4861;

    border-radius: 15px;

    padding: 24px;

    text-align: center;

    background:
        rgba(109,140,255,.025);
}

.upload-icon {
    font-size: 32px;

    margin-bottom: 8px;
}

.upload-text {
    font-size: 14px;

    font-weight: 600;

    margin-bottom: 5px;
}

.upload-sub {
    color: var(--muted);

    font-size: 12px;

    margin-bottom: 15px;
}

.upload-file {
    margin: 14px auto 12px;

    padding: 10px 12px;

    max-width: 100%;

    border-radius: 10px;

    background: var(--surface2);

    border: 1px solid var(--border);

    color: var(--text);

    font-size: 12px;

    text-align: left;

    overflow-wrap: anywhere;
}

.upload-file-label {
    color: var(--muted);

    font-size: 10px;

    text-transform: uppercase;

    letter-spacing: .5px;

    margin-bottom: 4px;
}

.upload-progress {
    display: none;

    margin: 14px 0 4px;

    text-align: left;
}

.upload-progress-bar {
    height: 10px;

    overflow: hidden;

    border-radius: 999px;

    background: #232d40;
}

.upload-progress-fill {
    width: 0%;

    height: 100%;

    border-radius: inherit;

    background:
        linear-gradient(
            90deg,
            var(--accent),
            var(--accent2)
        );

    transition: width .12s linear;
}

.upload-progress-info {
    display: flex;

    justify-content: space-between;

    gap: 10px;

    margin-top: 7px;

    color: var(--muted);

    font-size: 11px;
}

input[type=file] {
    display: none;
}

.empty {
    padding: 35px 15px;

    text-align: center;

    color: var(--muted);
}

.empty-icon {
    font-size: 35px;

    margin-bottom: 8px;
}

.server-info {
    margin-top: 18px;
}

.player {
    overflow: hidden;

    border-radius: 15px;

    background: #000;

    border:
        1px solid var(--border);
}

.player video,
.player audio,
.viewer-image {
    display: block;

    width: 100%;
}

.player video {
    max-height: 70vh;
}

.audio-player {
    padding: 30px 18px;
}

.viewer-image {
    max-height: 75vh;

    object-fit: contain;

    background: #05070b;
}

.viewer-title {
    margin:
        14px 0 5px;

    font-size: 16px;

    font-weight: 600;
}

.viewer-meta {
    color: var(--muted);

    font-size: 12px;
}

.text-viewer {
    margin: 0;

    padding: 18px;

    overflow: auto;

    max-height: 70vh;

    border-radius: 14px;

    background: #0a0e16;

    border:
        1px solid var(--border);

    color: #dce3ef;

    font-family:
        "Cascadia Code",
        "SFMono-Regular",
        Consolas,
        monospace;

    font-size: 13px;

    line-height: 1.6;

    white-space: pre-wrap;

    overflow-wrap: anywhere;
}

.back {
    display: inline-flex;

    align-items: center;

    gap: 6px;

    color: #b8c6ff;

    text-decoration: none;

    font-size: 13px;

    margin-bottom: 15px;
}

.back:hover {
    text-decoration: underline;
}

.create-folder {
    display: flex;

    gap: 8px;
}

.create-folder input {
    flex: 1;

    min-width: 0;

    padding: 10px 12px;

    border-radius: 9px;

    border:
        1px solid var(--border);

    background: var(--surface2);

    color: var(--text);

    outline: none;
}

.create-folder input:focus {
    border-color: var(--accent);
}

.footer {
    text-align: center;

    color: #626e81;

    font-size: 11px;

    margin-top: 22px;
}

@media (max-width: 650px) {

    .container {
        width: calc(100% - 18px);

        padding-top: 16px;
    }

    .header {
        align-items: flex-start;

        flex-direction: column;
    }

    .card {
        padding: 15px;

        border-radius: 15px;
    }

    .stats {
        grid-template-columns: 1fr;
    }

    .file {
        align-items: flex-start;
    }

    .file-actions {
        flex-direction: column;
    }

    .file-actions .btn {
        width: 85px;
    }

    .create-folder {
        flex-direction: column;
    }

    .upload {
        padding: 20px 12px;
    }
}

</style>

)rawliteral";
}

// ============================================================
// Build common header
// ============================================================

String pageHeader(String title)
{
  String html;

  html += R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>

<meta charset="UTF-8">

<meta
    name="viewport"
    content="width=device-width, initial-scale=1"
>

<meta
    name="theme-color"
    content="#080b12"
>

<title>)rawliteral";

  html += htmlEscape(title);

  html += R"rawliteral(</title>
)rawliteral";

  html += css();

  html += R"rawliteral(
</head>

<body>

<div class="container">

<header class="header">

    <div class="brand">

        <div class="logo">
            💾
        </div>

        <div>

            <h1>ESP32 Mini NAS</h1>

            <div class="subtitle">
                Personal network storage
            </div>

        </div>

    </div>

    <div class="status">
        <span class="status-dot"></span>
        Online
    </div>

</header>

)rawliteral";

  return html;
}

// ============================================================
// Page footer
// ============================================================

String pageFooter()
{
  return R"rawliteral(

<footer class="footer">
    ESP32 Mini NAS · Local Network
</footer>

</div>

</body>
</html>

)rawliteral";
}

// ============================================================
// Breadcrumbs
// ============================================================

String buildBreadcrumbs(String path)
{
  path = cleanPath(path);

  String html;

  html += "<div class='breadcrumbs'>";

  html += "<a href='/?path=/'>Home</a>";

  if (path != "/")
  {
    String current = "";

    int start = 1;

    while (start < path.length())
    {
      int slash = path.indexOf('/', start);

      String part;

      if (slash < 0)
      {
        part =
            path.substring(start);

        start =
            path.length();
      }
      else
      {
        part =
            path.substring(
                start,
                slash);

        start =
            slash + 1;
      }

      if (part.length() == 0)
        continue;

      current += "/";
      current += part;

      html +=
          "<span class='breadcrumb-separator'>/</span>";

      html +=
          String("<a href='/?path=") +
          urlEncode(current) +
          "'>" +
          htmlEscape(part) +
          "</a>";
    }
  }

  html += "</div>";

  return html;
}

// ============================================================
// Build NAS browser
// ============================================================

String buildBrowserPage(String path)
{
  path = cleanPath(path);

  if (path == "")
    path = "/";

  if (!SD.exists(path))
    path = "/";

  File directory = SD.open(path);

  if (!directory ||
      !directory.isDirectory())
  {
    if (directory)
      directory.close();

    path = "/";

    directory = SD.open(path);
  }

  String html =
      pageHeader("Files");

  // Storage card
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  uint64_t free = total - used;

  int percent =
      storagePercentage();

  html += R"rawliteral(

<section class="card">

    <div class="storage-head">

        <div>

            <div style="font-size:17px;font-weight:600">
                Storage
            </div>

            <div class="muted" style="font-size:12px;margin-top:3px">
                MicroSD card
            </div>

        </div>

        <div class="storage-percent">
)rawliteral";

  html += String(percent);

  html += R"rawliteral(%
        </div>

    </div>

    <div class="progress">

        <div
            class="progress-bar"
            style="width:)rawliteral";

  html += String(percent);

  html += R"rawliteral(%">
        </div>

    </div>

    <div class="stats">

        <div class="stat">
            <div class="stat-label">Total</div>
            <div class="stat-value">)rawliteral";

  html += formatBytes(total);

  html += R"rawliteral(</div>
        </div>

        <div class="stat">
            <div class="stat-label">Used</div>
            <div class="stat-value">)rawliteral";

  html += formatBytes(used);

  html += R"rawliteral(</div>
        </div>

        <div class="stat">
            <div class="stat-label">Available</div>
            <div class="stat-value">)rawliteral";

  html += formatBytes(free);

  html += R"rawliteral(</div>
        </div>

    </div>

</section>

)rawliteral";

  // Folder creation
  // Environment
  html += R"rawliteral(

<section class="card">

    <div class="section-head">

        <h2>Environment</h2>

        <span
            id="sensorStatus"
            class="muted"
            style="font-size:12px"
        >
            Reading DHT11...
        </span>

    </div>

    <div class="stats">

        <div class="stat">
            <div class="stat-label">🌡️ Temperature</div>
            <div
                id="temperatureValue"
                class="stat-value"
            >
                --.- °C
            </div>
        </div>

        <div class="stat">
            <div class="stat-label">💧 Humidity</div>
            <div
                id="humidityValue"
                class="stat-value"
            >
                --.- %
            </div>
        </div>

        <div class="stat">
            <div class="stat-label">Sensor</div>
            <div class="stat-value">DHT11</div>
        </div>

    </div>

</section>

<script>
(function () {
    const temperature =
        document.getElementById("temperatureValue");

    const humidity =
        document.getElementById("humidityValue");

    const status =
        document.getElementById("sensorStatus");

    async function updateSensor()
    {
        try {
            const response =
                await fetch("/sensor", {
                    cache: "no-store"
                });

            if (!response.ok) {
                throw new Error("Sensor unavailable");
            }

            const data =
                await response.json();

            if (!data.ok) {
                throw new Error(
                    data.error || "Sensor read failed"
                );
            }

            temperature.textContent =
                Number(data.temperature).toFixed(1) +
                " °C";

            humidity.textContent =
                Number(data.humidity).toFixed(1) +
                " %";

            status.textContent =
                "Live";

            status.style.color =
                "var(--green)";
        }
        catch (error)
        {
            temperature.textContent =
                "--.- °C";

            humidity.textContent =
                "--.- %";

            status.textContent =
                "Sensor error";

            status.style.color =
                "var(--red)";
        }
    }

    updateSensor();
    setInterval(updateSensor, 3000);
})();
</script>

)rawliteral";

  // New Folder
  html += R"rawliteral(

<section class="card">

    <div class="section-head">

        <h2>New Folder</h2>

    </div>

    <form
        class="create-folder"
        method="GET"
        action="/mkdir"
    >

        <input
            type="hidden"
            name="path"
            value=")rawliteral";

  html += htmlEscape(path);

  html += R"rawliteral("
        >

        <input
            type="text"
            name="name"
            placeholder="Folder name"
            required
            maxlength="32"
        >

        <button
            class="btn btn-primary"
            type="submit"
        >
            📁 Create
        </button>

    </form>

</section>

)rawliteral";

  // File browser
  html += R"rawliteral(

<section class="card">

    <div class="section-head">

        <h2>Files</h2>

        <span class="muted" style="font-size:12px">
            Browse storage
        </span>

    </div>

)rawliteral";

  html += buildBreadcrumbs(path);

  // Back button
  if (path != "/")
  {
    String parent =
        getParentPath(path);

    html +=
        String("<a class='back' href='/?path=") +
        urlEncode(parent) +
        "'>← Back</a>";
  }

  html += "<div class='file-list'>";

  int itemCount = 0;

  if (directory)
  {
    File item =
        directory.openNextFile();

    while (item)
    {
      itemCount++;

      String itemName =
          String(item.name());

      String displayName =
          getFilename(itemName);

      // ================================================
      // Directory
      // ================================================

      if (item.isDirectory())
      {
        String folderPath;

        if (path == "/")
        {
          folderPath = "/" + itemName;
        }
        else
        {
          folderPath = path + "/" + itemName;
        }

        folderPath = cleanPath(folderPath);

        html += R"rawliteral(

<div class="file folder">

    <div class="file-icon">
        📁
    </div>

    <div class="file-info">

        <div class="file-name">
)rawliteral";

        html +=
            htmlEscape(displayName);

        html += R"rawliteral(
        </div>

        <div class="file-meta">
            Folder
        </div>

    </div>

    <div class="file-actions">

        <a
            class="btn btn-primary"
            href="/?path=)rawliteral";

        html +=
            urlEncode(folderPath);

        html += R"rawliteral("
        >
            Open
        </a>

        <a
            class="btn btn-danger"
            href="/delete?file=)rawliteral";

        html +=
            urlEncode(folderPath);

        html += R"rawliteral("
            onclick="return confirm('Delete this folder and everything inside it?')"
        >
            🗑
        </a>

    </div>

</div>

)rawliteral";
      }

      // ================================================
      // File
      // ================================================

      else
      {
        String filePath;

        if (path == "/")
        {
          filePath = "/" + itemName;
        }
        else
        {
          filePath = path + "/" + itemName;
        }

        filePath = cleanPath(filePath);

        String encodedPath =
            urlEncode(filePath);

        String icon =
            getFileIcon(displayName);

        html += R"rawliteral(

<div class="file">

    <div class="file-icon">
)rawliteral";

        html += icon;

        html += R"rawliteral(
    </div>

    <div class="file-info">

        <div class="file-name">
)rawliteral";

        html +=
            htmlEscape(displayName);

        html += R"rawliteral(
        </div>

        <div class="file-meta">
)rawliteral";

        html +=
            formatBytes(item.size());

        html += R"rawliteral(
        </div>

    </div>

    <div class="file-actions">

)rawliteral";

        // View button
        if (
            isImage(displayName) ||
            isVideo(displayName) ||
            isAudio(displayName) ||
            isText(displayName))
        {
          html +=
              String("<a class='btn btn-primary' href='/view?file=") +
              encodedPath +
              "'>View</a>";
        }

        // Download
        html +=
            String("<a class='btn' href='/download?file=") +
            encodedPath +
            "'>↓</a>";

        // Delete
        html += String("<a class='btn btn-danger' ") +
                String("href='/delete?file=") +
                encodedPath +
                "' " +
                "onclick=\"return confirm('Delete this file?')\">" +
                "🗑</a>";

        html += R"rawliteral(

    </div>

</div>

)rawliteral";
      }

      item = directory.openNextFile();
    }

    directory.close();
  }

  if (itemCount == 0)
  {
    html += R"rawliteral(

<div class="empty">

    <div class="empty-icon">
        📂
    </div>

    This folder is empty.

</div>

)rawliteral";
  }

  html += "</div>";

  html += "</section>";

  // Upload
  html += R"rawliteral(

<section class="card">

    <div class="section-head">

        <h2>Upload</h2>

    </div>

    <form
        id="uploadForm"
        method="POST"
        action="/upload"
        enctype="multipart/form-data"
    >

        <input
            type="hidden"
            name="path"
            value=")rawliteral";

  html +=
      htmlEscape(path);

  html += R"rawliteral("
        >

        <div class="upload">

            <div class="upload-icon">
                📤
            </div>

            <div class="upload-text">
                Choose a file to upload
            </div>

            <div class="upload-sub">
                Current folder:
                )rawliteral";

  html +=
      htmlEscape(path);

  html += R"rawliteral(
            </div>

            <label class="btn btn-primary">
                Choose File

                <input
                    id="uploadFile"
                    type="file"
                    name="file"
                    required
                >
            </label>

            <div
                id="selectedFile"
                class="upload-file"
            >
                <div class="upload-file-label">
                    Selected file
                </div>

                <div id="selectedFileName">
                    No file selected
                </div>
            </div>

            <button
                id="uploadButton"
                class="btn"
                type="submit"
            >
                Upload
            </button>

            <div
                id="uploadProgress"
                class="upload-progress"
            >
                <div class="upload-progress-bar">
                    <div
                        id="uploadProgressFill"
                        class="upload-progress-fill"
                    ></div>
                </div>

                <div class="upload-progress-info">
                    <span id="uploadProgressText">
                        0%
                    </span>

                    <span id="uploadSpeedText">
                        0 B/s
                    </span>
                </div>
            </div>

        </div>

    </form>

</section>

<script>
(function () {
    const form =
        document.getElementById("uploadForm");

    const fileInput =
        document.getElementById("uploadFile");

    const fileName =
        document.getElementById("selectedFileName");

    const uploadButton =
        document.getElementById("uploadButton");

    const progress =
        document.getElementById("uploadProgress");

    const progressFill =
        document.getElementById("uploadProgressFill");

    const progressText =
        document.getElementById("uploadProgressText");

    const speedText =
        document.getElementById("uploadSpeedText");

    fileInput.addEventListener("change", function () {
        if (fileInput.files.length > 0) {
            fileName.textContent =
                fileInput.files[0].name;
        } else {
            fileName.textContent =
                "No file selected";
        }
    });

    form.addEventListener("submit", function (event) {
        event.preventDefault();

        if (!fileInput.files.length) {
            return;
        }

        const file =
            fileInput.files[0];

        const xhr =
            new XMLHttpRequest();

        const formData =
            new FormData(form);

        let lastLoaded = 0;
        let lastTime = performance.now();

        uploadButton.disabled = true;
        uploadButton.textContent =
            "Uploading...";

        progress.style.display =
            "block";

        progressFill.style.width =
            "0%";

        progressText.textContent =
            "0%";

        speedText.textContent =
            "0 B/s";

        xhr.upload.addEventListener(
            "progress",
            function (event) {
                if (!event.lengthComputable) {
                    return;
                }

                const percent =
                    Math.round(
                        (event.loaded /
                         event.total) * 100
                    );

                const now =
                    performance.now();

                const elapsed =
                    (now - lastTime) / 1000;

                if (elapsed > 0.05) {
                    const speed =
                        (event.loaded -
                         lastLoaded) /
                        elapsed;

                    speedText.textContent =
                        formatSpeed(speed);

                    lastLoaded =
                        event.loaded;

                    lastTime = now;
                }

                progressFill.style.width =
                    percent + "%";

                progressText.textContent =
                    formatBytes(event.loaded) +
                    " / " +
                    formatBytes(event.total) +
                    " (" +
                    percent +
                    "%)";
            }
        );

        xhr.addEventListener(
            "load",
            function () {
                if (
                    xhr.status >= 200 &&
                    xhr.status < 400
                ) {
                    progressFill.style.width =
                        "100%";

                    progressText.textContent =
                        formatBytes(file.size) +
                        " / " +
                        formatBytes(file.size) +
                        " (100%)";

                    uploadButton.textContent =
                        "Uploaded";

                    setTimeout(
                        function () {
                            window.location.reload();
                        },
                        500
                    );
                } else {
                    uploadButton.disabled =
                        false;

                    uploadButton.textContent =
                        "Upload";

                    alert(
                        "Upload failed: HTTP " +
                        xhr.status
                    );
                }
            }
        );

        xhr.addEventListener(
            "error",
            function () {
                uploadButton.disabled =
                    false;

                uploadButton.textContent =
                    "Upload";

                alert(
                    "Upload failed. Check the ESP32 connection."
                );
            }
        );

        xhr.addEventListener(
            "abort",
            function () {
                uploadButton.disabled =
                    false;

                uploadButton.textContent =
                    "Upload";
            }
        );

        xhr.open(
            "POST",
            form.action +
                "?size=" +
                encodeURIComponent(file.size),
            true
        );

        xhr.send(formData);
    });

    function formatBytes(bytes) {
        if (bytes < 1024)
            return bytes.toFixed(0) + " B";

        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) +
                   " KB";

        if (bytes < 1024 * 1024 * 1024)
            return (bytes / 1024 / 1024).toFixed(1) +
                   " MB";

        return (bytes / 1024 / 1024 / 1024).toFixed(2) +
               " GB";
    }

    function formatSpeed(bytesPerSecond) {
        if (bytesPerSecond < 1024)
            return bytesPerSecond.toFixed(0) +
                   " B/s";

        if (bytesPerSecond < 1024 * 1024)
            return (bytesPerSecond / 1024).toFixed(1) +
                   " KB/s";

        return (bytesPerSecond / 1024 / 1024).toFixed(2) +
               " MB/s";
    }
})();
</script>

)rawliteral";

  html += pageFooter();

  return html;
}

// ============================================================
// DHT11 JSON endpoint
// ============================================================

void handleSensor()
{
  updateDHT();

  if (isnan(currentTemperature) ||
      isnan(currentHumidity))
  {
    server.send(
        503,
        "application/json; charset=utf-8",
        R"({"ok":false,"error":"DHT11 read failed"})");
    return;
  }

  String json = R"({"ok":true,"temperature":)";
  json += String(currentTemperature, 1);
  json += R"(,"humidity":)";
  json += String(currentHumidity, 1);
  json += "}";

  server.send(
      200,
      "application/json; charset=utf-8",
      json);
}

// ============================================================
// Root browser
// ============================================================

void handleRoot()
{
  String path = "/";

  if (server.hasArg("path"))
  {
    path =
        server.arg("path");
  }

  path =
      cleanPath(path);

  if (path == "")
    path = "/";

  server.send(
      200,
      "text/html; charset=utf-8",
      buildBrowserPage(path));
}

// ============================================================
// Create folder
// ============================================================

void handleMkdir()
{
  if (
      !server.hasArg("path") ||
      !server.hasArg("name"))
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Missing folder information");

    return;
  }

  String parent =
      cleanPath(
          server.arg("path"));

  String name =
      server.arg("name");

  if (parent == "")
    parent = "/";

  // Folder-name security
  if (
      name.length() == 0 ||
      name.indexOf('/') >= 0 ||
      name.indexOf('\\') >= 0 ||
      name.indexOf("..") >= 0)
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Invalid folder name");

    return;
  }

  String newPath;

  if (parent == "/")
    newPath = String("/") + name;
  else
    newPath = parent + String("/") + name;

  if (SD.exists(newPath))
  {
    server.send(
        409,
        "text/plain; charset=utf-8",
        "Folder already exists");

    return;
  }

  if (!SD.mkdir(newPath))
  {
    server.send(
        500,
        "text/plain; charset=utf-8",
        "Could not create folder");

    return;
  }

  server.sendHeader(
      "Location",
      String("/?path=") + urlEncode(parent));

  server.send(303);
}

// ============================================================
// Delete file or folder
// ============================================================

bool deletePathRecursive(const String &path)
{
  File target = SD.open(path);

  if (!target)
    return false;

  if (!target.isDirectory())
  {
    target.close();
    return SD.remove(path);
  }

  File child = target.openNextFile();

  while (child)
  {
    String childName = String(child.name());
    String childPath;

    if (childName.startsWith("/"))
      childPath = childName;
    else if (path == "/")
      childPath = "/" + childName;
    else
      childPath = path + "/" + childName;

    childPath = cleanPath(childPath);

    child.close();

    if (!deletePathRecursive(childPath))
    {
      target.close();
      return false;
    }

    child = target.openNextFile();
  }

  target.close();

  return SD.rmdir(path);
}

void handleDelete()
{
  if (!server.hasArg("file"))
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Missing path");

    return;
  }

  String path =
      cleanPath(
          server.arg("file"));

  if (
      path == "" ||
      path == "/")
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Invalid path");

    return;
  }

  if (!SD.exists(path))
  {
    server.send(
        404,
        "text/plain; charset=utf-8",
        "Not found");

    return;
  }

  if (!deletePathRecursive(path))
  {
    server.send(
        409,
        "text/plain; charset=utf-8",
        "Could not delete file or folder.");

    return;
  }

  String parent =
      getParentPath(path);

  server.sendHeader(
      "Location",
      String("/?path=") +
          urlEncode(parent));

  server.send(303);
}

// ============================================================
// Upload

// ============================================================

void handleUpload()
{
  HTTPUpload &upload =
      server.upload();

  if (
      upload.status ==
      UPLOAD_FILE_START)
  {
    String filename =
        upload.filename;

    // Remove browser directory prefix
    int slash =
        filename.lastIndexOf('/');

    int backslash =
        filename.lastIndexOf('\\');

    int separator =
        max(slash, backslash);

    if (separator >= 0)
    {
      filename =
          filename.substring(
              separator + 1);
    }

    if (
        filename.length() == 0 ||
        filename.indexOf("..") >= 0)
    {
      return;
    }

    String folder = "/";

    if (server.hasArg("path"))
    {
      folder =
          cleanPath(
              server.arg("path"));
    }

    if (folder == "")
      folder = "/";

    uploadPath =
        folder == "/"
            ? String("/") + filename
            : folder + String("/") + filename;

    if (SD.exists(uploadPath))
    {
      SD.remove(uploadPath);
    }

    uploadFile =
        SD.open(
            uploadPath,
            FILE_WRITE);

    uploadInProgress = true;

    uploadBytesReceived = 0;

    if (server.hasArg("size"))
    {
      uploadTotalBytes =
          strtoull(
              server.arg("size").c_str(),
              nullptr,
              10);
    }
    else
    {
      uploadTotalBytes = 0;
    }

    uploadStartMillis = millis();
    uploadLastUpdateMillis = uploadStartMillis;
    uploadLastBytes = 0;
    uploadSpeedBps = 0.0;

    showUploadOLED();
  }

  else if (
      upload.status ==
      UPLOAD_FILE_WRITE)
  {
    if (uploadFile)
    {
      size_t written =
          uploadFile.write(
              upload.buf,
              upload.currentSize);

      uploadBytesReceived += written;

      // If the browser did not provide the file size,
      // use the library's cumulative size as a fallback.
      if (uploadTotalBytes == 0)
        uploadTotalBytes = upload.totalSize;

      unsigned long now = millis();
      unsigned long elapsed =
          now - uploadLastUpdateMillis;

      // Update speed often enough to look live,
      // without constantly redrawing the OLED.
      if (elapsed >= 250)
      {
        uint64_t bytesDelta =
            uploadBytesReceived - uploadLastBytes;

        uploadSpeedBps =
            (double)bytesDelta /
            ((double)elapsed / 1000.0);

        uploadLastBytes =
            uploadBytesReceived;

        uploadLastUpdateMillis = now;

        showUploadOLED();
      }
    }
  }

  else if (
      upload.status ==
      UPLOAD_FILE_END)
  {
    if (uploadFile)
    {
      uploadFile.close();
    }

    if (uploadBytesReceived < upload.totalSize)
      uploadBytesReceived = upload.totalSize;

    if (uploadTotalBytes == 0)
      uploadTotalBytes = uploadBytesReceived;

    unsigned long now = millis();
    unsigned long elapsed =
        now - uploadStartMillis;

    if (elapsed > 0)
    {
      uploadSpeedBps =
          (double)uploadBytesReceived /
          ((double)elapsed / 1000.0);
    }

    showUploadOLED();

    uploadInProgress = false;

    // Keep the final upload percentage/speed
    // visible until the normal OLED refresh.
    lastOLEDRefresh = millis();
  }

  else if (
      upload.status ==
      UPLOAD_FILE_ABORTED)
  {
    if (uploadFile)
    {
      uploadFile.close();
    }

    uploadInProgress = false;

    if (SD.exists(uploadPath))
    {
      SD.remove(uploadPath);
    }

    // Return to the normal OLED after cancellation.
    refreshNormalOLED();
  }
}

// ============================================================
// Media streaming with HTTP Range support

// ============================================================

void handleMedia()
{
  if (!server.hasArg("file"))
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Missing file");
    return;
  }

  String path = cleanPath(server.arg("file"));

  if (path == "" || !SD.exists(path))
  {
    server.send(
        404,
        "text/plain; charset=utf-8",
        "File not found");
    return;
  }

  File file = SD.open(path, FILE_READ);

  if (!file || file.isDirectory())
  {
    if (file)
      file.close();

    server.send(
        404,
        "text/plain; charset=utf-8",
        "File unavailable");
    return;
  }

  uint64_t fileSize = file.size();

  if (fileSize == 0)
  {
    file.close();

    server.send(
        200,
        getMimeType(path),
        "");

    return;
  }

  uint64_t start = 0;
  uint64_t end = fileSize - 1;

  bool partial = false;

  // ==========================================================
  // Handle HTTP Range request
  // ==========================================================

  if (server.hasHeader("Range"))
  {
    String range = server.header("Range");

    if (range.startsWith("bytes="))
    {
      range = range.substring(6);

      int dash = range.indexOf('-');

      if (dash >= 0)
      {
        String startString =
            range.substring(0, dash);

        String endString =
            range.substring(dash + 1);

        if (startString.length() > 0)
        {
          start =
              strtoull(
                  startString.c_str(),
                  nullptr,
                  10);
        }

        if (endString.length() > 0)
        {
          end =
              strtoull(
                  endString.c_str(),
                  nullptr,
                  10);
        }
        else
        {
          end = fileSize - 1;
        }

        if (start >= fileSize || start > end)
        {
          file.close();

          WiFiClient client =
              server.client();

          client.print(
              "HTTP/1.1 416 Range Not Satisfiable\r\n"
              "Content-Range: bytes */");

          client.print(fileSize);

          client.print(
              "\r\n"
              "Connection: close\r\n"
              "\r\n");

          client.stop();

          return;
        }

        if (end >= fileSize)
        {
          end = fileSize - 1;
        }

        partial = true;
      }
    }
  }

  uint64_t contentLength =
      end - start + 1;

  file.seek(start);

  String mime =
      getMimeType(path);

  // ==========================================================
  // IMPORTANT:
  // Send the HTTP response manually.
  // Do NOT use server.send() here.
  // ==========================================================

  WiFiClient client =
      server.client();

  if (!client || !client.connected())
  {
    file.close();
    return;
  }

  if (partial)
  {
    client.print(
        "HTTP/1.1 206 Partial Content\r\n");
  }
  else
  {
    client.print(
        "HTTP/1.1 200 OK\r\n");
  }

  client.print(
      "Content-Type: ");

  client.print(mime);

  client.print(
      "\r\n");

  client.print(
      "Content-Length: ");

  client.print(
      String(contentLength));

  client.print(
      "\r\n");

  client.print(
      "Accept-Ranges: bytes\r\n");

  if (partial)
  {
    client.print(
        "Content-Range: bytes ");

    client.print(
        String(start));

    client.print(
        "-");

    client.print(
        String(end));

    client.print(
        "/");

    client.print(
        String(fileSize));

    client.print(
        "\r\n");
  }

  client.print(
      "Cache-Control: no-cache\r\n");

  client.print(
      "Connection: close\r\n");

  client.print(
      "\r\n");

  // ==========================================================
  // Stream file
  // ==========================================================

  uint8_t buffer[4096];

  uint64_t remaining =
      contentLength;

  while (
      remaining > 0 &&
      client.connected())
  {
    size_t chunk =
        remaining > sizeof(buffer)
            ? sizeof(buffer)
            : (size_t)remaining;

    size_t bytesRead =
        file.read(
            buffer,
            chunk);

    if (bytesRead == 0)
    {
      break;
    }

    size_t bytesWritten =
        client.write(
            buffer,
            bytesRead);

    if (bytesWritten == 0)
    {
      break;
    }

    remaining -= bytesWritten;

    delay(0);
  }

  file.close();

  delay(1);

  client.stop();
}

// ============================================================
// Download
// ============================================================

void handleDownload()
{
  if (!server.hasArg("file"))
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Missing file");

    return;
  }

  String path =
      cleanPath(
          server.arg("file"));

  if (
      path == "" ||
      !SD.exists(path))
  {
    server.send(
        404,
        "text/plain; charset=utf-8",
        "File not found");

    return;
  }

  File file =
      SD.open(path);

  if (!file || file.isDirectory())
  {
    server.send(
        404,
        "text/plain; charset=utf-8",
        "File unavailable");

    return;
  }

  String filename =
      getFilename(path);

  server.sendHeader(
      "Content-Disposition",
      String("attachment; filename=\"") +
          filename +
          "\"");

  server.streamFile(
      file,
      "application/octet-stream");

  file.close();
}

// ============================================================
// Viewer
// ============================================================

String buildViewerPage(String path)
{
  path =
      cleanPath(path);

  if (
      path == "" ||
      !SD.exists(path))
  {
    return pageHeader("Not Found") +
           String("<section class='card'>") +
           "<h2>File not found</h2>" +
           "</section>" +
           pageFooter();
  }

  String filename =
      getFilename(path);

  String encoded =
      urlEncode(path);

  String html =
      pageHeader(filename);

  String parent =
      getParentPath(path);

  html +=
      String("<a class='back' href='/?path=") +
      urlEncode(parent) +
      "'>← Back to files</a>";

  // ========================================================
  // IMAGE
  // ========================================================

  if (isImage(filename))
  {
    html += R"rawliteral(

<section class="card">

    <div class="viewer-title">
)rawliteral";

    html +=
        htmlEscape(filename);

    html += R"rawliteral(
    </div>

    <div class="viewer-meta">
        )rawliteral";

    File f = SD.open(path);

    if (f)
    {
      html +=
          formatBytes(f.size());

      f.close();
    }

    html += R"rawliteral(
    </div>

    <div class="player" style="margin-top:15px">

        <img
            class="viewer-image"
            src="/media?file=)rawliteral";

    html += encoded;

    html += R"rawliteral("
            alt=")rawliteral";

    html +=
        htmlEscape(filename);

    html += R"rawliteral("
        >

    </div>

</section>

)rawliteral";
  }

  // ========================================================
  // VIDEO
  // ========================================================

  else if (isVideo(filename))
  {
    html += R"rawliteral(

<section class="card">

    <div class="viewer-title">
)rawliteral";

    html +=
        htmlEscape(filename);

    html += R"rawliteral(
    </div>

    <div class="viewer-meta">
        Video
    </div>

    <div
        class="player"
        style="margin-top:15px"
    >

        <video
            controls
            playsinline
            preload="metadata"
        >

            <source
                src="/media?file=)rawliteral";

    html += encoded;

    html += R"rawliteral("
                type=")rawliteral";

    html +=
        getMimeType(filename);

    html += R"rawliteral("
            >

            Your browser does not support video playback.

        </video>

    </div>

</section>

)rawliteral";
  }

  // ========================================================
  // AUDIO
  // ========================================================

  else if (isAudio(filename))
  {
    html += R"rawliteral(

<section class="card">

    <div class="viewer-title">
)rawliteral";

    html +=
        htmlEscape(filename);

    html += R"rawliteral(
    </div>

    <div class="viewer-meta">
        Audio
    </div>

    <div
        class="player audio-player"
        style="margin-top:15px"
    >

        <div style="
            font-size:42px;
            margin-bottom:15px;
        ">
            🎵
        </div>

        <audio
            controls
            preload="metadata"
            style="width:100%"
        >

            <source
                src="/media?file=)rawliteral";

    html += encoded;

    html += R"rawliteral("
                type=")rawliteral";

    html +=
        getMimeType(filename);

    html += R"rawliteral("
            >

            Your browser does not support audio playback.

        </audio>

    </div>

</section>

)rawliteral";
  }

  // ========================================================
  // TEXT
  // ========================================================

  else if (isText(filename))
  {
    html += R"rawliteral(

<section class="card">

    <div class="viewer-title">
)rawliteral";

    html +=
        htmlEscape(filename);

    html += R"rawliteral(
    </div>

    <div class="viewer-meta">
        Text / source file
    </div>

    <pre
        id="textViewer"
        class="text-viewer"
        style="margin-top:15px"
    >Loading...</pre>

</section>


<script>

fetch(
    "/media?file=)rawliteral";

    html += encoded;

    html += R"rawliteral("
)
.then(response => response.text())
.then(text => {

    document.getElementById(
        "textViewer"
    ).textContent = text;

})
.catch(error => {

    document.getElementById(
        "textViewer"
    ).textContent =
        "Could not load file.";

});

</script>

)rawliteral";
  }

  // ========================================================
  // Unsupported
  // ========================================================

  else
  {
    html += R"rawliteral(

<section class="card">

    <div class="empty">

        <div class="empty-icon">
            📄
        </div>

        <div>
            Preview unavailable
        </div>

        <br>

        <a
            class="btn btn-primary"
            href="/download?file=)rawliteral";

    html += encoded;

    html += R"rawliteral("
        >
            ↓ Download File
        </a>

    </div>

</section>

)rawliteral";
  }

  html += pageFooter();

  return html;
}

// ============================================================
// Viewer route
// ============================================================

void handleView()
{
  if (!server.hasArg("file"))
  {
    server.send(
        400,
        "text/plain; charset=utf-8",
        "Missing file");

    return;
  }

  String path =
      cleanPath(
          server.arg("file"));

  server.send(
      200,
      "text/html; charset=utf-8",
      buildViewerPage(path));
}

// ============================================================
// Wi-Fi
// ============================================================

bool connectWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD);

  int attempts = 0;

  while (
      WiFi.status() != WL_CONNECTED &&
      attempts < 40)
  {
    delay(500);

    attempts++;
  }

  return WiFi.status() ==
         WL_CONNECTED;
}

// ============================================================
// Setup
// ============================================================

void refreshNormalOLED()
{
  updateDHT();

  int percent = storagePercentage();

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ESP32 MINI NAS");

  display.setCursor(0, 16);
  if (!isnan(currentTemperature))
  {
    display.print("Temp: ");
    display.print(currentTemperature, 1);
    display.println(" C");
  }
  else
  {
    display.println("Temp: --.- C");
  }

  display.setCursor(0, 32);
  if (!isnan(currentHumidity))
  {
    display.print("Hum : ");
    display.print(currentHumidity, 1);
    display.println(" %");
  }
  else
  {
    display.println("Hum : --.- %");
  }

  display.setCursor(0, 48);
  display.print("SD: ");
  display.print(percent);
  display.println("% used");

  display.display();
}

void setup()
{
  Serial.begin(115200);

  delay(500);

  // ========================================================
  // DHT11
  // ========================================================

  dht.begin();

  // ========================================================
  // OLED
  // ========================================================

  Wire.begin(
      OLED_SDA,
      OLED_SCL);

  if (!display.begin(
          SSD1306_SWITCHCAPVCC,
          0x3C))
  {
    while (true)
    {
      delay(1000);
    }
  }

  showOLED(
      "ESP32 MINI NAS",
      "Starting...");

  delay(1000);

  // ========================================================
  // SD
  // ========================================================

  showOLED(
      "SD CARD",
      "Initializing...");

  spi.begin(
      SD_SCK,
      SD_MISO,
      SD_MOSI,
      SD_CS);

  if (!SD.begin(
          SD_CS,
          spi))
  {
    showOLED(
        "SD CARD ERROR",
        "Card not found");

    while (true)
    {
      delay(1000);
    }
  }

  showOLED(
      "SD CARD READY",
      formatBytes(
          SD.totalBytes()));

  delay(1000);

  // ========================================================
  // Wi-Fi
  // ========================================================

  showOLED(
      "WI-FI",
      "Connecting...");

  if (!connectWiFi())
  {
    showOLED(
        "WI-FI ERROR",
        "Connection failed");

    while (true)
    {
      delay(1000);
    }
  }

  // ========================================================
  // HTTP Range header
  // ========================================================

  const char *headerKeys[] = {
      "Range"};

  server.collectHeaders(
      headerKeys,
      1);

  // ========================================================
  // Routes
  // ========================================================

  server.on(
      "/",
      HTTP_GET,
      handleRoot);

  server.on(
      "/view",
      HTTP_GET,
      handleView);

  server.on(
      "/media",
      HTTP_GET,
      handleMedia);

  server.on(
      "/download",
      HTTP_GET,
      handleDownload);

  server.on(
      "/delete",
      HTTP_GET,
      handleDelete);

  server.on(
      "/mkdir",
      HTTP_GET,
      handleMkdir);

  server.on(
      "/sensor",
      HTTP_GET,
      handleSensor);

  server.on(
      "/upload",
      HTTP_POST,

      []()
      {
        String path = "/";

        if (server.hasArg("path"))
        {
          path =
              cleanPath(
                  server.arg("path"));
        }

        server.sendHeader(
            "Location",
            String("/?path=") +
                urlEncode(path));

        server.send(303);
      },

      handleUpload);

  // ========================================================
  // Start
  // ========================================================

  updateDHT();

  server.begin();

  String ip =
      WiFi.localIP().toString();

  showOLED(
      "MINI NAS ONLINE",
      ip,
      "SD: READY",
      "HTTP :80");

  Serial.println();
  Serial.println(
      "================================");

  Serial.println(
      "       ESP32 MINI NAS");

  Serial.println(
      "================================");

  Serial.print(
      "IP Address: ");

  Serial.println(ip);

  Serial.println(
      "Server ready.");

  Serial.println();
}

// ============================================================
// Loop
// ============================================================

void loop()
{
  server.handleClient();

  if (
      !uploadInProgress &&
      millis() - lastOLEDRefresh >=
          OLED_REFRESH_INTERVAL)
  {
    lastOLEDRefresh = millis();

    refreshNormalOLED();
  }
}