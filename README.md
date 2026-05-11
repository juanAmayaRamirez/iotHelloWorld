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

## Step 9: Store Data in S3 (Partitioned)

This creates an S3 bucket and an IoT Rule that automatically saves every message to S3, organized by date for easy querying later.

**Data will be stored as:**
```
s3://YOUR_BUCKET/dt=2026-05-10/1715370000.json
```

### 9.1 Create the S3 bucket

```bash
aws s3api create-bucket --bucket esp32-sensor-data-<YOUR_ACCOUNT_ID> --region us-east-1
```

> Replace `<YOUR_ACCOUNT_ID>` with your AWS account ID. Bucket names must be globally unique.

### 9.2 Create an IAM role for the IoT Rule

The IoT Rule needs permission to write to S3. First, create the trust policy:

```bash
cat > iot-s3-trust-policy.json << 'EOF'
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Principal": { "Service": "iot.amazonaws.com" },
    "Action": "sts:AssumeRole"
  }]
}
EOF
```

Create the role:

```bash
aws iam create-role \
  --role-name iot-s3-rule-role \
  --assume-role-policy-document file://iot-s3-trust-policy.json
```

Attach a policy that allows writing to your bucket:

```bash
cat > iot-s3-policy.json << EOF
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": "s3:PutObject",
    "Resource": "arn:aws:s3:::esp32-sensor-data-<YOUR_ACCOUNT_ID>/*"
  }]
}
EOF

aws iam put-role-policy \
  --role-name iot-s3-rule-role \
  --policy-name iot-s3-put \
  --policy-document file://iot-s3-policy.json
```

### 9.3 Create the IoT Rule

This rule listens to the `esp32/sensor/distance` topic and writes each message to S3, partitioned by date:

```bash
cat > iot-s3-rule.json << EOF
{
  "sql": "SELECT *, timestamp() as ts FROM 'esp32/sensor/distance'",
  "awsIotSqlVersion": "2016-03-23",
  "actions": [{
    "s3": {
      "bucketName": "esp32-sensor-data-<YOUR_ACCOUNT_ID>",
      "key": "dt=\${parse_time(\"yyyy-MM-dd\", timestamp())}/\${timestamp()}.json",
      "roleArn": "arn:aws:iam::<YOUR_ACCOUNT_ID>:role/iot-s3-rule-role"
    }
  }]
}
EOF

aws iot create-topic-rule \
  --rule-name esp32_to_s3 \
  --topic-rule-payload file://iot-s3-rule.json \
  --region us-east-1
```

### 9.4 Verify data lands in S3

After the ESP32 publishes a few messages (or run `python publish_test.py`), check:

```bash
aws s3 ls s3://esp32-sensor-data-<YOUR_ACCOUNT_ID>/ --recursive --region us-east-1
```

You should see files like:
```
dt=2026-05-10/1715370000123.json
dt=2026-05-10/1715370002456.json
```

Each file contains one JSON message:
```json
{"distance": 25, "unit": "cm", "ts": 1715370000123}
```

---

---

## Step 10: Store Data in AWS IoT SiteWise

SiteWise is a managed service for collecting, organizing, and monitoring industrial IoT data. Unlike S3 (which stores raw files), SiteWise gives you built-in time-series storage, dashboards, and asset hierarchy.

### 10.1 Create an Asset Model

An asset model defines what properties your sensor has:

```bash
aws iotsitewise create-asset-model \
  --asset-model-name "DistanceSensorModel" \
  --asset-model-properties '[{
    "name": "distance",
    "dataType": "INTEGER",
    "unit": "cm",
    "type": {"measurement": {}}
  }]' \
  --region us-east-1
```

Save the `assetModelId` from the output.

### 10.2 Create an Asset

An asset is an instance of the model (your physical sensor):

```bash
aws iotsitewise create-asset \
  --asset-name "ESP32-Sensor" \
  --asset-model-id <assetModelId> \
  --region us-east-1
```

Save the `assetId`. Then get the `propertyId` for "distance":

```bash
aws iotsitewise describe-asset --asset-id <assetId> --region us-east-1
```

Look for the property named "distance" in the output and save its `id`.

> ⚠️ Wait until the asset status is `ACTIVE` before proceeding (takes ~1 minute).

### 10.3 Create IAM Role for the Rule

```bash
cat > iot-sitewise-trust.json << 'EOF'
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Principal": { "Service": "iot.amazonaws.com" },
    "Action": "sts:AssumeRole"
  }]
}
EOF

aws iam create-role \
  --role-name iot-sitewise-rule-role \
  --assume-role-policy-document file://iot-sitewise-trust.json

aws iam put-role-policy \
  --role-name iot-sitewise-rule-role \
  --policy-name iot-sitewise-put \
  --policy-document '{
    "Version": "2012-10-17",
    "Statement": [{
      "Effect": "Allow",
      "Action": "iotsitewise:BatchPutAssetPropertyValue",
      "Resource": "*"
    }]
  }'
```

### 10.4 Create the IoT Rule

This rule takes each message from the ESP32 and writes the distance value into SiteWise:

```bash
cat > iot-sitewise-rule.json << EOF
{
  "sql": "SELECT * FROM 'esp32/sensor/distance'",
  "awsIotSqlVersion": "2016-03-23",
  "actions": [{
    "iotSiteWise": {
      "putAssetPropertyValueEntries": [{
        "assetId": "<assetId>",
        "propertyId": "<propertyId>",
        "propertyValues": [{
          "timestamp": { "timeInSeconds": "\${floor(timestamp() / 1E3)}", "offsetInNanos": "0" },
          "value": { "integerValue": "\${distance}" }
        }]
      }],
      "roleArn": "arn:aws:iam::<YOUR_ACCOUNT_ID>:role/iot-sitewise-rule-role"
    }
  }]
}
EOF

aws iot create-topic-rule \
  --rule-name esp32_to_sitewise \
  --topic-rule-payload file://iot-sitewise-rule.json \
  --region us-east-1
```

### 10.5 Verify data in SiteWise

After publishing a few messages, check the property value:

```bash
aws iotsitewise get-asset-property-value-history \
  --asset-id <assetId> \
  --property-id <propertyId> \
  --region us-east-1
```

You can also view the data in the AWS Console: **IoT SiteWise** → **Assets** → **ESP32-Sensor** → **distance**.

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
