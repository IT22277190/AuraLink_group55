import paho.mqtt.client as mqtt
import json
import time
import random
import math

# MQTT Configuration
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC = "auralink/sensor/data"

# Connect to MQTT Broker
client = mqtt.Client()
client.connect(MQTT_BROKER, MQTT_PORT, 60)

def generate_sensor_data():
    # Simulate temperature between 20-30°C with some variation
    temp = 25 + 5 * math.sin(time.time() / 10)
    # Simulate humidity between 40-60%
    humidity = 50 + 10 * math.cos(time.time() / 8)
    # Simulate light level (0-100%)
    light = abs(50 + 30 * math.sin(time.time() / 15))
    # Simulate NOx level (0-100%)
    nox = abs(30 + 20 * math.sin(time.time() / 12))
    
    return {
        "temperature": round(temp, 1),
        "humidity": round(humidity, 1),
        "light_percent": round(light),
        "nox_percent": round(nox)
    }

print("Starting MQTT test publisher...")
print("Press Ctrl+C to stop")

try:
    while True:
        data = generate_sensor_data()
        print(f"Publishing: {data}")
        client.publish(MQTT_TOPIC, json.dumps(data))
        time.sleep(2)  # Publish every 2 seconds

except KeyboardInterrupt:
    print("\nStopping publisher...")
    client.disconnect()