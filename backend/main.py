import os
import json
import time
import threading
import paho.mqtt.client as mqtt
try:
    import openai
except Exception:
    openai = None
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# --- Configuration ---
# Fetch configuration from environment variables
MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "test.mosquitto.org")
MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", 1883))
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

# MQTT Topics
TOPIC_SENSOR_DATA = "auralink/sensor/data"
TOPIC_DISPLAY_QUOTE = "auralink/display/quote"
TOPIC_DISPLAY_SUMMARY = "auralink/display/summary"
TOPIC_URGENCY_LED = "auralink/urgency/led"

# OpenAI availability check
if openai is None:
    print("WARNING: openai package not installed. LLM features will be disabled.")
else:
    if OPENAI_API_KEY:
        openai.api_key = OPENAI_API_KEY
    else:
        print("WARNING: OPENAI_API_KEY environment variable not set. LLM features will be disabled.")

# --- Mock Email Service ---
# In a real-world application, this would use an API (e.g., Gmail API)
# to fetch actual emails. For this project, we'll cycle through mock emails.
mock_emails = [
    {
        "sender": "boss@work.com",
        "subject": "Urgent: Project Deadline Moved Up",
        "body": "Hi team, please be advised that the deadline for the Q3 report has been moved to this Friday. All hands on deck to get this finalized. I need the preliminary draft by EOD tomorrow. Thanks."
    },
    {
        "sender": "newsletter@tech.io",
        "subject": "This Week in AI",
        "body": "Explore the latest advancements in generative models, a deep dive into reinforcement learning, and our predictions for the next wave of AI startups. Plus, a new dataset for computer vision enthusiasts has just been released."
    },
    {
        "sender": "mom@family.com",
        "subject": "Dinner this weekend?",
        "body": "Hey sweetie, hope you're having a good week. I was wondering if you're free to come over for dinner on Saturday evening? Let me know if you can make it. Love, Mom."
    }
]
email_index = 0

def get_latest_email():
    """Fetches the next mock email in the list, cycling through them."""
    global email_index
    email = mock_emails[email_index]
    email_index = (email_index + 1) % len(mock_emails)
    # Return the full email content for processing
    return f"From: {email['sender']}\nSubject: {email['subject']}\n\n{email['body']}"

# --- OpenAI LLM Interaction Functions ---
def call_openai_api(prompt, max_tokens=60):
    """Generic function to call the OpenAI Chat Completion API."""
    try:
        response = openai.chat.completions.create(
            model="gpt-3.5-turbo",
            messages=[
                {"role": "system", "content": "You are a helpful assistant."},
                {"role": "user", "content": prompt}
            ],
            max_tokens=max_tokens,
            temperature=0.7,
        )
        return response.choices[0].message.content.strip()
    except Exception as e:
        print(f"Error calling OpenAI API: {e}")
        return None

def generate_literary_quote(temp, humidity):
    """Generates a context-aware literary quote based on sensor data."""
    print(f"Generating quote for Temp: {temp}°C, Humidity: {humidity}%")
    prompt = f"Generate a short, original, and inspiring literature-style quote (like one from a classic novel) that reflects the mood of a room with a temperature of {temp}°C and {humidity}% humidity. The quote should be less than 150 characters."
    return call_openai_api(prompt)

def summarize_email(email_content):
    """Summarizes an email using the LLM."""
    print("Summarizing email...")
    prompt = f"Summarize the following email into a single, concise sentence suitable for a small display (less than 150 characters):\n\n---\n{email_content}\n---"
    return call_openai_api(prompt)

def analyze_email_urgency(email_content):
    """Analyzes email urgency and returns LOW, MEDIUM, or HIGH."""
    print("Analyzing email urgency...")
    prompt = f"Analyze the urgency of the following email. Respond with only ONE word: LOW, MEDIUM, or HIGH.\n\n---\n{email_content}\n---"
    urgency = call_openai_api(prompt, max_tokens=5)
    if urgency and urgency.upper() in ["LOW", "MEDIUM", "HIGH"]:
        return urgency.upper()
    return "LOW" # Default to LOW on failure

# --- MQTT Client Logic ---
def on_connect(client, userdata, flags, rc):
    """Callback for when the client connects to the MQTT broker."""
    if rc == 0:
        print("Connected to MQTT Broker!")
        client.subscribe(TOPIC_SENSOR_DATA)
        print(f"Subscribed to topic: {TOPIC_SENSOR_DATA}")
    else:
        print(f"Failed to connect, return code {rc}\n")

def process_sensor_data(payload_str):
    """The main processing logic for incoming sensor data."""
    try:
        data = json.loads(payload_str)
        temp = data.get("temperature")
        humidity = data.get("humidity")

        if temp is None or humidity is None:
            print("Invalid sensor data received.")
            return

        print(f"Received Sensor Data -> Temp: {temp}°C, Humidity: {humidity}%")
        
        # 1. Get a new quote
        quote = generate_literary_quote(temp, humidity)
        if quote:
            client.publish(TOPIC_DISPLAY_QUOTE, quote)
            print(f"Published to `{TOPIC_DISPLAY_QUOTE}`: {quote}")

        # 2. Get and process the latest email
        email_content = get_latest_email()
        summary = summarize_email(email_content)
        urgency = analyze_email_urgency(email_content)

        if summary:
            client.publish(TOPIC_DISPLAY_SUMMARY, summary)
            print(f"Published to `{TOPIC_DISPLAY_SUMMARY}`: {summary}")
        
        if urgency:
            client.publish(TOPIC_URGENCY_LED, urgency)
            print(f"Published to `{TOPIC_URGENCY_LED}`: {urgency}")

    except json.JSONDecodeError:
        print("Error decoding JSON payload.")
    except Exception as e:
        print(f"An error occurred in process_sensor_data: {e}")


def on_message(client, userdata, msg):
    """Callback for when a message is received from the broker."""
    payload_str = msg.payload.decode('utf-8')
    # Use a thread to process the data to avoid blocking the MQTT loop
    processing_thread = threading.Thread(target=process_sensor_data, args=(payload_str,))
    processing_thread.start()


# --- Main Application Execution ---
if __name__ == "__main__":
    print("Starting AuraLink Backend Service...")
    
    # Initialize MQTT Client
    client = mqtt.Client(client_id="auralink-backend-service")
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"Connecting to MQTT broker at {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}...")
    try:
        client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
    except Exception as e:
        print(f"Could not connect to MQTT Broker: {e}")
        exit(1)

    # Start the network loop in a non-blocking way
    client.loop_forever()