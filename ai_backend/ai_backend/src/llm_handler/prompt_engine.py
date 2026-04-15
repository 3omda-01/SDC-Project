def get_adaptive_strategies(subtype, physiological_state):
    """
    Selects clinical strategies based on the ADHD subtype 
    and immediate physiological state.
    """
    # Subtype-specific long-term strategies
    base_strategies = {
        "Inattentive": [
            "Task chunking: Break assignments into 10-minute intervals.",
            "Visual checklists to minimize reliance on working memory."
        ],
        "Hyperactive-Impulsive": [
            "Movement breaks: Scheduled 2-minute activity periods.",
            "Immediate positive reinforcement for completing tasks."
        ],
        "Combined": [
            "Simplified instructions: One-step directions at a time.",
            "Quiet workspace with minimal visual clutter."
        ]
    }
    
    strategies = base_strategies.get(subtype, ["General focus support strategies."])
    
    # Real-time Intervention (The 'Digital Pharmacy' layer)
    if physiological_state == "Stressed":
        strategies.insert(0, "🔴 INTERVENTION: High physiological arousal detected. Initiate a 'Box Breathing' exercise (4s inhale, 4s hold, 4s exhale).")
    elif physiological_state == "Fatigued":
        strategies.insert(0, "🟡 INTERVENTION: Cognitive fatigue detected. End the current task and switch to a low-stimulation sensory activity.")
        
    return strategies

def get_parent_feedback_prompt(risk_index, subtype, trends, physiological_state="Normal"):
    """
    Generates a structured prompt for the LLM to provide supportive feedback.
    """
    specific_strategies = get_adaptive_strategies(subtype, physiological_state)
    strategies_text = "\n- ".join(specific_strategies)
    
    return f"""
    You are the HealEdu AI Assistant, an expert adjunct for early ADHD screening.
    
    [SESSION DATA]
    - Risk Index: {risk_index}
    - Subtype: {subtype}
    - 6-Month Trend: {trends}
    - Current State: {physiological_state}

    [CLINICAL STRATEGIES]
    - {strategies_text}

    [INSTRUCTIONS]
    1. Explain how the child's {physiological_state} state is impacting their performance.
    2. Provide a supportive, empathetic explanation of the trend ({trends}).
    3. MANDATORY: 'This is an AI-generated screening adjunct and not a clinical diagnosis. Consult a medical professional for formal assessment.'
    4. Tone: Grounded, non-alarmist, and encouraging.
    """