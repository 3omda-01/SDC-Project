import gradio as gr
from src.inference.behavioral_logic import BehavioralModel
from src.inference.stress_model import StressModel
from src.llm_handler.prompt_engine import get_parent_feedback_prompt

# Initialize our logic engines
behavior_engine = BehavioralModel()
stress_engine = StressModel()
stress_engine.train_mock_model() # Ensure model is ready

def run_healedu_dashboard(student_id, rt_val, omissions, commissions, hr, hrv, resp):
    # 1. Run the ML Stress Prediction
    state = stress_engine.predict_state(hr, hrv, resp)
    
    # 2. Run Behavioral Analysis
    # We simulate the list of reaction times for the demo
    analysis = behavior_engine.calculate_risk_index({
        'rt': [rt_val] * 10, 
        'omissions': omissions, 
        'commissions': commissions, 
        'total_trials': 100,
        'avg_hr': hr
    })
    
    # 3. Generate the AI Guidance
    prompt = get_parent_feedback_prompt(
        risk_index=analysis['risk_score'],
        subtype=analysis['estimated_subtype'],
        trends="Stable (Real-time Simulation)",
        physiological_state=state
    )
    
    # Return formatted results for the UI
    return (
        f"Subtype: {analysis['estimated_subtype']}",
        f"Risk Index: {analysis['risk_score']}",
        f"Physical State: {state}",
        prompt
    )

# Build the UI Layout
with gr.Blocks(title="HealEdu AI Educator Dashboard") as demo:
    gr.Markdown("# 🧠 HealEdu AI Core Control Center")
    gr.Markdown("Adjust student metrics below to see the AI analyze behavior and physiology in real-time.")
    
    with gr.Row():
        with gr.Column():
            student_id = gr.Textbox(label="Student ID", value="STUDENT_001")
            rt_slider = gr.Slider(0.1, 2.0, value=0.6, label="Mean Reaction Time (seconds)")
            omissions = gr.Number(label="Omission Errors (Inattention)", value=5)
            commissions = gr.Number(label="Commission Errors (Impulsivity)", value=2)
        
        with gr.Column():
            hr_slider = gr.Slider(60, 160, value=85, label="Avg Heart Rate (BPM)")
            hrv_slider = gr.Slider(5, 100, value=50, label="HRV Index")
            resp_slider = gr.Slider(10, 40, value=18, label="Respiration Rate")
            
    btn = gr.Button("Analyze Session", variant="primary")
    
    gr.Markdown("### 📊 Analysis Results")
    with gr.Row():
        res_subtype = gr.Textbox(label="Detected Subtype")
        res_risk = gr.Textbox(label="Risk Score")
        res_state = gr.Textbox(label="Physiological State")
    
    res_prompt = gr.TextArea(label="Generated AI Guidance for Parents/Teachers", lines=10)

    btn.click(
        fn=run_healedu_dashboard, 
        inputs=[student_id, rt_slider, omissions, commissions, hr_slider, hrv_slider, resp_slider], 
        outputs=[res_subtype, res_risk, res_state, res_prompt]
    )

if __name__ == "__main__":
    demo.launch()