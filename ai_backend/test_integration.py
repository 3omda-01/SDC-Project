import requests
import json

# The URL of your local FastAPI server
URL = "http://127.0.0.1:8000/analyze"

def verify_healedu_system():
    # Simulated data: High HR + High Omissions = High Risk + Stressed State
    test_payload = {
        "student_id": "STUDENT_001",
        "rt": [0.75, 0.82, 0.78, 0.90],
        "omissions": 25,          # High inattention
        "commissions": 10,        # Moderate impulsivity
        "total_trials": 100,
        "avg_hr": 125.0,          # High Heart Rate
        "hrv_index": 10.0,         # Low HRV (Stress marker)
        "resp_rate": 30.0,         # High Respiration
        "trend_context": "Stable"
    }

    print("--- Starting System Integration Test ---")
    try:
        response = requests.post(URL, json=test_payload)
        
        if response.status_code == 200:
            result = response.json()
            
            print("\n✅ API CONNECTION: SUCCESS")
            print(f"📊 ML PREDICTION (State): {result['analysis']['physiological_state']}")
            print(f"🎯 RISK SCORE: {result['analysis']['risk_score']}")
            print(f"🧠 SUBTYPE: {result['analysis']['estimated_subtype']}")
            
            print("\n--- GENERATED LLM PROMPT ---")
            print(result['llm_prompt'])
            
            # Verify the Adaptive Logic
            if "INTERVENTION" in result['llm_prompt'] and result['analysis']['physiological_state'] == "Stressed":
                print("\n✅ ADAPTIVE LOGIC: SUCCESS (Intervention was triggered)")
            else:
                print("\n⚠️ ADAPTIVE LOGIC: WARNING (Check intervention logic)")
                
        else:
            print(f"❌ API ERROR: {response.status_code}")
            
    except Exception as e:
        print(f"❌ CONNECTION FAILED: Is the uvicorn server running? Error: {e}")

if __name__ == "__main__":
    verify_healedu_system()