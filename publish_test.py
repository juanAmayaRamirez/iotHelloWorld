import json
import time
import random
import paho.mqtt.client as mqtt
import ssl

endpoint = "aexyf84z25uw2-ats.iot.us-east-1.amazonaws.com"
cert = "certs/cert.pem"
key = "certs/private.key"
ca = "certs/AmazonRootCA1.pem"
topic = "esp32/sensor/distance"

client = mqtt.Client()
client.tls_set(ca_certs=ca, certfile=cert, keyfile=key, tls_version=ssl.PROTOCOL_TLSv1_2)
client.connect(endpoint, 8883)
client.loop_start()

while True:
    distance = random.randint(2, 100)
    payload = json.dumps({"distance": distance, "unit": "cm"})
    client.publish(topic, payload)
    print(f"Published: {payload}")
    time.sleep(2)
