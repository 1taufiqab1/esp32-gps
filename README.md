# [Link Project Velxio versi dulu Klik Disini](https://velxio.dev/1taufiqab1/gps-tracker/)

# ESP32 - GPS

![cover1](https://lh3.googleusercontent.com/d/12CEH3CdnXrtFGARBeO9lhAtevzsOuNnk)

![cover2](https://lh3.googleusercontent.com/d/1Gfi19k2lcbCBz2L6g7iS7LXVj-fUL-bP)

## 1. Komponen dan Library

### Hardware
- ESP32 DevKit V1 30 pin CP2102
- GPS NEO-6M u-blox
- Breadboard
- 2 LED
- 1 Resistor
- 2 Male to Male Jumper
- 6 Female to Male Jumper

### Library Arduino
- `WiFi` — koneksi WiFi.
- `WiFiClientSecure` — keamanan koneksi WiFi.
- `PubSubClient` — komunikasi MQTT.
- `TinyGPSPlus` — membaca dan mengolah data GPS.
- `ArduinoJson` — parsing perintah JSON.
- `string.h` — operasi string.
- `time.h` — mendapatkan waktu sistem.
- `stdint.h` — tipe data integer dengan ukuran tertentu.

## 2. Konfigurasi Pin

| Komponen | Pin Komponen | Pin ESP32 |
|---|---|---|
| GPS NEO-6M | VCC | 3V3 |
| | GND | GND |
| | TX | GPIO 16 (RX2) |
| | RX | GPIO 17 (TX2) |
| LED A | Anoda (+) | GPIO 32 |
| | Katoda (-) | GND (resistor) |
| LED B | Anoda (+) | GPIO 33 |
| | Katoda (-) | GND (resistor) |

## 3. Konfigurasi MQTT

### MQTT Broker

> Note: *Contoh value MQTT Broker pada broker public*

| Konfigurasi | Nilai |
|---|---|
| Broker | `test.mosquitto.org` |
| Port | `1884` |
| Username | `rw` |
| Password | `readwrite` |
| Keep Alive | 60 detik |
| Device ID | `esp32:001` |

### MQTT Topics

| Topic | Fungsi |
|---|---|
| `tracker/devices/esp32:001/telemetry` | Mengirim data GPS |
| `tracker/devices/esp32:001/command` | Menerima perintah |

## 4. Fitur Utama

### A. GPS Tracking

ESP32 membaca data dari modul GPS menggunakan library TinyGPSPlus.

- Latitude: garis lintang.
- Longitude: garis bujur.
- Speed: kecepatan dalam km/h.
- Satellites: jumlah satelit yang terdeteksi oleh GPS.
- Timestamp: waktu Unix dalam detik (UTC).

### B. Pengiriman Data MQTT

Perangkat mengirimkan data GPS secara berkala ke topic telemetry.

Interval awal pengiriman adalah:

```cpp
bool gps_enabled = false;
int interval_seconds = 10;
```

GPS secara default dalam kondisi OFF, sehingga belum mengirimkan telemetry sampai diaktifkan melalui MQTT.

Interval pengiriman yang diperbolehkan adalah:

- 2 detik
- 5 detik
- 10 detik
- 30 detik

Contoh payload telemetry:

```json
{
  "device_id": "esp32:001",
  "lat": -7.257500,
  "lng": 112.752100,
  "speed": 4.3522,
  "satellites": 3,
  "timestamp": 1794700000
}
```

### C. Pengujian Koneksi MQTT

Pengujian dapat dilakukan menggunakan HiveMQ WebSocket Client:

```url
https://www.hivemq.com/demos/websocket-client/
```

Konfigurasi koneksi:

| Konfigurasi | Nilai |
|---|---|
| Host | `test.mosquitto.org` |
| Port | `8091` |
| Username | `rw` |
| Password | `readwrite` |

Untuk melihat komunikasi perangkat, lakukan subscribe ke topic:

```text
tracker/devices/esp32:001/#
```

Untuk mengirim command ke ESP32, gunakan topic:

```text
tracker/devices/esp32:001/command
```

**Mengaktifkan GPS:**

Perintah harus dikirim dalam format JSON dengan device_id, active, dan timestamp.

```json
{
  "device_id": "esp32:001",
  "active": true,
  "timestamp": 1795590000
}
```

Apabila `interval_seconds` tidak disertakan ketika GPS diaktifkan, firmware menggunakan interval default 10 detik.  

**Menonaktifkan GPS:**

```json
{
  "device_id": "esp32:001",
  "active": false,
  "timestamp": 1795590010
}
```

**Mengubah Interval:**

Mengubah interval menjadi 5 detik:

```json
{
  "device_id": "esp32:001",
  "active": true,
  "interval_seconds": 5,
  "timestamp": 1795590020
}
```
Field timestamp harus berupa integer Unix timestamp yang valid dan tidak lebih kecil daripada timestamp command sebelumnya.

Field interval_seconds bersifat opsional. Firmware menerima nilai 0, 2, 5, 10, dan 30. Apabila GPS diaktifkan dengan nilai 0, interval otomatis diubah menjadi 10 detik.

## 5. Indikator LED

Terdapat dua LED untuk menunjukkan kondisi perangkat.

### LED A — WiFi dan MQTT

| Kondisi | Indikator |
|---|---|
| WiFi terputus | Berkedip cepat (150 ms) |
| WiFi terhubung, MQTT terputus | Berkedip lambat (500 ms) |
| WiFi dan MQTT terhubung | Menyala terus |

### LED B — GPS

| Kondisi | Indikator |
|---|---|
| GPS dinonaktifkan | Mati |
| GPS belum mendapatkan lokasi valid | Berkedip lambat (500 ms) |
| GPS mendapatkan lokasi valid | Menyala terus |
| Data berhasil dikirim | Mati sesaat sebagai indikator pengiriman |

## 6. Alur Kerja Sistem

1. ESP32 melakukan inisialisasi hardware.
2. ESP32 menghubungkan ke WiFi.
3. ESP32 menginisialisasi MQTT dan menghubungkan ke broker.
4. Setelah terhubung, ESP32 melakukan subscribe ke topic command.
5. ESP32 menerima perintah melalui topik /command untuk mengaktifkan atau menonaktifkan GPS serta mengatur interval pengiriman.
6. Ketika lokasi valid dan interval terpenuhi, data dikirim ke MQTT.
7. LED diperbarui sesuai kondisi WiFi, MQTT, dan GPS.

## 7 Mengatur Variable `secrets.h`

1. Buat file `secrets.h` di `sketch/` **atau** ubah nama file `secrets.example.h` menjadi `secrets.h`.
2. Ubah nilai variable di dalam `secrets.h`(format mengacu pada `secrets.example.h`).
3. Isi variable WIFI, MQTT, dan CERT nya sesuai dengan nilai yang sudah ditetapkan.

## 8. Catatan

- Inteval pengiriman data default 10 detik.

