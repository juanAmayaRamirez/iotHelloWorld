# LAB 2 - ESP32 Ultrasonic Sensor Publishing to AWS IoT Core

## Overview

In this lab you will connect an ESP32 microcontroller to AWS IoT Core over the internet. The ESP32 reads distance from an ultrasonic sensor and sends it to the cloud every 2 seconds using the MQTT protocol (a lightweight messaging protocol designed for IoT devices).

**What happens:**

```
HC-SR04 Sensor → ESP32 → WiFi → Internet → AWS IoT Core
                                                 ↓
                                          MQTT Topic:
                                    esp32/sensor/distance
                                                 ↓
                                    {"distance":25,"unit":"cm"}
```

The ESP32 also turns on an LED when the measured distance is greater than 10 cm.

---

## Prerequisites

- ESP32 dev board (MH-ET LIVE or similar)
- HC-SR04 ultrasonic sensor
- LED + 220–330Ω resistor
- Jumper wires + breadboard
- USB cable (data, not charge-only)
- AWS account with CLI configured (`aws configure`)
- PlatformIO installed (VS Code extension or CLI)
- WiFi network (must be **2.4GHz** — ESP32 does not support 5GHz)

---

## Step 1: Wire the Circuit

| Component | Pin | ESP32 GPIO |
|-----------|-----|------------|
| HC-SR04 VCC | VCC | **5V (VIN)** — not 3.3V! |
| HC-SR04 Trig | Trig | GPIO 26 |
| HC-SR04 Echo | Echo | GPIO 27 |
| HC-SR04 GND | GND | GND |
| LED (+) | Anode | GPIO 25 (through 220–330Ω resistor) |
| LED (–) | Cathode | GND |

> ⚠️ The HC-SR04 requires 5V to operate. At 3.3V it will always read 0.

---

## Step 2: Clone This Repo

```bash
git clone https://github.com/YOUR_USERNAME/iotHelloWorld.git
cd iotHelloWorld
```

---

## Step 3: Create an AWS IoT Thing and Certificates

This registers your ESP32 as a "Thing" in AWS and generates the security certificates it needs to connect.

### 3.1 Create the Thing

```bash
aws iot create-thing --thing-name esp32-sensor --region us-east-1
```

### 3.2 Create certificates

First create the `certs/` folder, then generate the certificates directly into it:

```bash
mkdir -p certs
aws iot create-keys-and-certificate \
  --set-as-active \
  --certificate-pem-outfile certs/cert.pem \
  --public-key-outfile certs/public.key \
  --private-key-outfile certs/private.key \
  --region us-east-1
```

**Save the `certificateArn` from the output** — you'll need it in the next steps. It looks like:
```
arn:aws:iot:us-east-1:123456789012:cert/abc123...
```

### 3.3 Create a policy

This policy allows the device to publish/subscribe to any topic (restrict in production):

```bash
aws iot create-policy --policy-name esp32-policy \
  --policy-document '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Action":"iot:*","Resource":"*"}]}' \
  --region us-east-1
```

### 3.4 Attach the policy to your certificate

Replace `<certificateArn>` with the ARN from step 3.2:

```bash
aws iot attach-policy --policy-name esp32-policy \
  --target <certificateArn> --region us-east-1
```

### 3.5 Attach the certificate to your Thing

```bash
aws iot attach-thing-principal --thing-name esp32-sensor \
  --principal <certificateArn> --region us-east-1
```

### 3.6 Get your endpoint

```bash
aws iot describe-endpoint --endpoint-type iot:Data-ATS --region us-east-1
```

This returns something like:
```json
{ "endpointAddress": "xxxxxx-ats.iot.us-east-1.amazonaws.com" }
```

**Copy this endpoint** — you'll need it in the next step.

---

## Step 4: Download the Amazon Root CA

```bash
curl -o certs/AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

Your `certs/` folder should now contain:
```
certs/
├── AmazonRootCA1.pem   (Amazon's root certificate)
├── cert.pem            (your device certificate)
└── private.key         (your device private key)
```

---

## Step 5: Create Your secrets.h File

Copy the template:

```bash
cp include/secrets.h.example include/secrets.h
```

Now open `include/secrets.h` and fill in:

1. **WiFi credentials** — replace `YOUR_SSID` and `YOUR_PASSWORD` with your 2.4GHz WiFi network name and password

2. **MQTT endpoint** — replace `YOUR_ENDPOINT-ats.iot.us-east-1.amazonaws.com` with the endpoint from Step 3.6

3. **Certificates** — open each file in `certs/` and paste its contents between the `-----BEGIN` and `-----END` markers:
   - `certs/AmazonRootCA1.pem` → paste into the `rootCA` variable
   - `certs/cert.pem` → paste into the `clientCert` variable
   - `certs/private.key` → paste into the `privateKey` variable

> ⚠️ `secrets.h` is in `.gitignore` — it will never be pushed to GitHub. This keeps your credentials safe.

---

## Step 6: Build and Upload

### Build (verify it compiles)

```bash
pio run
```

### Upload to ESP32

If using WSL with USB passthrough:

1. In PowerShell (admin): `usbipd attach --wsl --busid <YOUR_BUSID>`
2. On the ESP32: Hold **BOOT** → Press **EN** → Release **BOOT**
3. Run:

```bash
pio run --target upload
```

---

## Step 7: Monitor Serial Output

```bash
pio device monitor
```

You should see:
```
Connecting to WiFi... connected
Connecting to AWS IoT... connected
Publishing: {"distance":25,"unit":"cm"}
Publishing: {"distance":24,"unit":"cm"}
```

---

## Step 8: Verify in AWS Console

1. Go to **AWS IoT Core** → **MQTT test client**
2. Subscribe to topic: `esp32/sensor/distance`
3. You should see messages arriving every 2 seconds:

```json
{"distance": 25, "unit": "cm"}
```

---

## Python Test Publisher (Optional)

You can also publish test data from your PC without the ESP32:

```bash
pip install paho-mqtt
python publish_test.py
```

This sends random distance values to the same topic using your certs.

---

## Project Structure

```
iotHelloWorld/
├── src/
│   └── main.cpp              ← Main code (no secrets here)
├── include/
│   ├── secrets.h             ← Your credentials (gitignored)
│   └── secrets.h.example     ← Template showing what to fill in
├── certs/                    ← Your certificates (gitignored)
├── publish_test.py           ← Python test publisher
├── platformio.ini            ← Build configuration
└── README.md                 ← This file
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `Distance: 0 cm` | HC-SR04 needs 5V, not 3.3V. Check wiring. |
| `Connecting to WiFi...` forever | ESP32 only supports 2.4GHz. Use the 2.4GHz SSID. |
| Upload fails at 40-50% | Lower upload speed (already set to 115200). Use BOOT+EN method. Re-attach USB in WSL. |
| `csum err` on boot | Flash was corrupted. Run `pio run --target erase` then re-upload. |
| MQTT `failed, rc=-2` | Check endpoint URL and certificates are correct in secrets.h. |

---

## platformio.ini

```ini
[env:mhetesp32devkit]
platform = espressif32
board = mhetesp32devkit
framework = arduino
monitor_speed = 115200
upload_speed = 115200
lib_deps = knolleary/PubSubClient@2.8
```
