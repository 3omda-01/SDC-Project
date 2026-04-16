from fastapi import FastAPI
from pydantic import BaseModel
from typing import List, Optional
from src.inference.behavioral_logic import BehavioralModel
from src.llm_handler.prompt_engine import get_parent_feedback_prompt
from src.llm_handler.llm_client import LLMClient, check_ollama
from src.utils.report_formatter import ReportFormatter
from src.inference.stress_model import StressModel

app = FastAPI(title="HealEdu AI Core API")
behavior_engine = BehavioralModel()
reporter = ReportFormatter()
stress_engine = StressModel()
llm_client = LLMClient()

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

@app.get("/health")
def health_check():
    ollama_ok = llm_client.is_available()
    return {
        "status": "OK",
        "ollama": "connected" if ollama_ok else "not running",
        "models": llm_client.list_models() if ollama_ok else []
    }

@app.post("/analyze")
def analyze_session(data: SessionData):
    state = stress_engine.predict_state(data.avg_hr, data.hrv_index, data.resp_rate)

    analysis = behavior_engine.calculate_risk_index({
        'rt': data.rt, 
        'omissions': data.omissions, 
        'commissions': data.commissions, 
        'total_trials': data.total_trials,
        'avg_hr': data.avg_hr
    })
    
    prompt = get_parent_feedback_prompt(
        risk_index=analysis['risk_score'],
        subtype=analysis['estimated_subtype'],
        trends=data.trend_context,
        physiological_state=state
    )
    
    analysis["physiological_state"] = state
    
    # Call LLM if available
    llm_response = None
    llm_available = False
    
    if llm_client.is_available():
        llm_response = llm_client.generate_feedback(prompt)
        llm_available = True
    
    return {
        "analysis": analysis,
        "llm_prompt": prompt,
        "llm_response": llm_response,
        "llm_available": llm_available
    }

@app.post("/llm/chat")
def llm_chat(message: str, system: Optional[str] = None):
    if not llm_client.is_available():
        return {"error": "Ollama not available", "llm_available": False}
    
    messages = [{"role": "user", "content": message}]
    
    result = llm_client.chat(messages=messages, system=system)
    
    if "error" in result:
        return {"error": result["error"], "llm_available": True}
    
    return {
        "response": result.get("message", {}).get("content", ""),
        "llm_available": True
    }

@app.get("/report/{student_id}")
def get_longitudinal_report(student_id: str):
    report_text = reporter.generate_summary(student_id)
    return {"report": report_text}