from fastapi import FastAPI, HTTPException, status, WebSocket
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import asyncio
import json
import os
import logging
from typing import List, Dict, Any
from pydantic import BaseModel
import paho.mqtt.client as mqtt
import httpx
from dotenv import load_dotenv

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Load environment variables
load_dotenv()

# Configuration
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")
if not OPENAI_API_KEY:
    raise ValueError("OPENAI_API_KEY must be set in .env file")

MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "test.mosquitto.org")
MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))

# FastAPI app instance
app = FastAPI(
    title="AuraLink API",
    description="AuraLink IoT Backend API for processing sensor data and generating smart responses",
    version="1.0.0"
)

# Mount static files directory
app.mount("/static", StaticFiles(directory="static"), name="static")

# WebSocket connections store
active_connections: List[WebSocket] = []

# MQTT Topics
TOPIC_SENSOR_DATA = "auralink/sensor/data"
TOPIC_QUOTE = "auralink/display/quote"
TOPIC_SUMMARY = "auralink/display/summary"
TOPIC_URGENCY = "auralink/urgency/led"

# Pydantic model for sensor data
class SensorData(BaseModel):
    temperature: float
    humidity: float
    light_percent: int = 0
    nox_percent: int = 0

async def broadcast_message(topic: str, payload: Dict):
    if not active_connections:
        return
    
    message = {}
    if topic == TOPIC_SENSOR_DATA:
        message = {
            "type": "sensor_data",
            **payload
        }
    elif topic == TOPIC_QUOTE:
        message = {
            "type": "display_message",
            "quote": payload.get("quote", ""),
            "summary": ""
        }
    elif topic == TOPIC_SUMMARY:
        message = {
            "type": "display_message",
            "quote": "",
            "summary": payload.get("summary", "")
        }
    elif topic == TOPIC_URGENCY:
        message = {
            "type": "urgency",
            "level": payload.get("level", "LOW")
        }

    if message:
        for connection in active_connections:
            try:
                await connection.send_json(message)
            except Exception as e:
                logger.error(f"Failed to send message to WebSocket client: {e}")
                active_connections.remove(connection)

@app.get("/")
async def get_index():
    return FileResponse('static/web_interface.html')

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    active_connections.append(websocket)
    try:
        while True:
            # Keep the connection alive
            await websocket.receive_text()
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        active_connections.remove(websocket)

# Mock emails for demonstration
MOCK_EMAILS = [
    "Urgent: Server downtime detected! Immediate action required for system recovery.",
    "Weekly team meeting tomorrow at 10 AM. Please prepare your progress updates.",
    "New feature release scheduled for next week. Testing feedback needed.",
    "Holiday party planning - Looking for volunteers to help organize."
]

# Global variables
current_email_index = 0
mqtt_client = mqtt.Client()

def get_latest_email() -> str:
    """Simulate getting the latest email by cycling through mock emails."""
    global current_email_index
    email = MOCK_EMAILS[current_email_index]
    current_email_index = (current_email_index + 1) % len(MOCK_EMAILS)
    return email

async def generate_literary_quote(temp: float, humidity: float) -> str:
    """Generate a literary-style quote based on temperature and humidity."""
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.openai.com/v1/chat/completions",
            headers={"Authorization": f"Bearer {OPENAI_API_KEY}"},
            json={
                "model": "gpt-3.5-turbo",
                "messages": [{
                    "role": "system",
                    "content": "You are a poetic writer who creates brief, atmospheric quotes under 150 characters."
                }, {
                    "role": "user",
                    "content": f"Create a brief, literary quote that captures the mood of {temp}°C temperature and {humidity}% humidity. Response must be under 150 characters."
                }]
            }
        )
        return response.json()["choices"][0]["message"]["content"].strip()

async def summarize_email(email_content: str) -> str:
    """Summarize email content into a single, display-friendly sentence."""
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.openai.com/v1/chat/completions",
            headers={"Authorization": f"Bearer {OPENAI_API_KEY}"},
            json={
                "model": "gpt-3.5-turbo",
                "messages": [{
                    "role": "system",
                    "content": "You are a concise summarizer. Provide single-sentence summaries suitable for a 20x4 LCD display."
                }, {
                    "role": "user",
                    "content": f"Summarize this email in one short sentence (max 80 characters): {email_content}"
                }]
            }
        )
        return response.json()["choices"][0]["message"]["content"].strip()

async def analyze_email_urgency(email_content: str) -> str:
    """Analyze email urgency and return LOW, MEDIUM, or HIGH."""
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.openai.com/v1/chat/completions",
            headers={"Authorization": f"Bearer {OPENAI_API_KEY}"},
            json={
                "model": "gpt-3.5-turbo",
                "messages": [{
                    "role": "system",
                    "content": "You are an email urgency classifier. Only respond with exactly one word: LOW, MEDIUM, or HIGH."
                }, {
                    "role": "user",
                    "content": f"Classify the urgency of this email. Response must be exactly LOW, MEDIUM, or HIGH: {email_content}"
                }]
            }
        )
        return response.json()["choices"][0]["message"]["content"].strip()

def on_mqtt_connect(client, userdata, flags, rc):
    """Callback for when the client receives a CONNACK response from the server."""
    if rc == 0:
        logger.info("Connected to MQTT broker")
    else:
        logger.error(f"Failed to connect to MQTT broker with code: {rc}")

@app.on_event("startup")
async def startup_event():
    """Initialize MQTT client on startup."""
    try:
        mqtt_client.on_connect = on_mqtt_connect
        mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT)
        mqtt_client.loop_start()
        logger.info("MQTT client initialized")
    except Exception as e:
        logger.error(f"Failed to initialize MQTT client: {str(e)}")
        raise

@app.on_event("shutdown")
async def shutdown_event():
    """Clean up MQTT client on shutdown."""
    try:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        logger.info("MQTT client shutdown complete")
    except Exception as e:
        logger.error(f"Error during MQTT client shutdown: {str(e)}")

@app.post("/data", status_code=status.HTTP_202_ACCEPTED)
async def process_sensor_data(data: SensorData):
    """Process incoming sensor data and generate responses."""
    try:
        # Get the latest email
        email = get_latest_email()
        
        # Process all tasks concurrently
        quote, summary, urgency = await asyncio.gather(
            generate_literary_quote(data.temperature, data.humidity),
            summarize_email(email),
            analyze_email_urgency(email)
        )
        
        # Publish results to MQTT topics
        mqtt_client.publish(TOPIC_QUOTE, quote)
        mqtt_client.publish(TOPIC_SUMMARY, summary)
        mqtt_client.publish(TOPIC_URGENCY, urgency)
        
        return {"message": "Data processed successfully"}
    
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=str(e)
        )

# For development server
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)