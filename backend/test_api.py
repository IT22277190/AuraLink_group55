import requests
import json

def test_sensor_endpoint():
    url = "http://localhost:8000/data"
    data = {
        "temperature": 25.5,
        "humidity": 60.0
    }
    
    try:
        response = requests.post(url, json=data)
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.json()}")
    except Exception as e:
        print(f"Error: {str(e)}")

if __name__ == "__main__":
    test_sensor_endpoint()