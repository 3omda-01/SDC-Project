from fastapi import FastAPI
from pydantic import BaseModel
from typing import List
from src.inference.behavioral_logic import BehavioralModel
from src.llm_handler.prompt_engine import get_parent_feedback_prompt
from src.utils.report_formatter import ReportFormatter
from src.inference.stress_model import StressModel

app = FastAPI(title="HealEdu AI Core API")
behavior_engine = BehavioralModel()
reporter = ReportFormatter()
stress_engine = StressModel()

# Train at startup
stress_engine.train_mock_model()

class SessionData(BaseModel):
    student_id: str
    rt: List[float]
    omissions: int
    commissions: int
    total_trials: int
    avg_hr: float
    hrv_index: float
    resp_rate: float
    trend_context: str = "Stable"

@app.get("/")
def read_root():
    return {"status": "HealEdu AI Core is Online"}

@app.post("/analyze")
def analyze_session(data: SessionData):
    # 1. Predict state using ML (Stressed, Fatigued, or Calm)
    state = stress_engine.predict_state(data.avg_hr, data.hrv_index, data.resp_rate)

    # 2. Run Behavioral Inference
    analysis = behavior_engine.calculate_risk_index({
        'rt': data.rt, 
        'omissions': data.omissions, 
        'commissions': data.commissions, 
        'total_trials': data.total_trials,
        'avg_hr': data.avg_hr
    })
    
    # 3. CRITICAL FIX: Ensure the prompt engine gets the ML 'state'
    prompt = get_parent_feedback_prompt(
        risk_index=analysis['risk_score'],
        subtype=analysis['estimated_subtype'],
        trends=data.trend_context,
        physiological_state=state # This was the missing link!
    )
    
    # Add state to analysis for the frontend to see
    analysis["physiological_state"] = state
    
    return {
        "analysis": analysis,
        "llm_prompt": prompt
    }

@app.get("/report/{student_id}")
def get_longitudinal_report(student_id: str):
    report_text = reporter.generate_summary(student_id)
    return {"report": report_text}