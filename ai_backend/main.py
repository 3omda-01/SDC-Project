import pandas as pd
import os
from src.inference.behavioral_logic import BehavioralModel
from src.llm_handler.prompt_engine import get_parent_feedback_prompt

def run_heal_edu_core():
    # 1. Check if data exists
    data_path = "data/longitudinal_sample.csv"
    if not os.path.exists(data_path):
        print("Error: Data file not found. Please run data_generator.py first.")
        return

    # 2. Load the data
    df = pd.read_csv(data_path)
    
    # 3. Simulate analysis on the 'Current' session (the last row of the CSV)
    last_row = df.iloc[-1]
    current_session = {
        'rt': [last_row['mean_rt']], 
        'omissions': last_row['omission_errors'],
        'commissions': last_row['commission_errors'],
        'total_trials': 100 
    }
    
    # Initialize the engine and analyze
    engine = BehavioralModel()
    analysis = engine.calculate_risk_index(current_session)
    
    # 4. Determine trend (Compare last session to the 6-month average)
    avg_omissions = df['omission_errors'].mean()
    trend_status = "Improving" if last_row['omission_errors'] < avg_omissions else "Attention Required"

    # 5. Generate the final LLM Prompt
    final_prompt = get_parent_feedback_prompt(
        risk_index=analysis['risk_score'],
        subtype=analysis['estimated_subtype'],
        trends=trend_status
    )

    # Output the results
    print("\n" + "="*50)
    print("HEALEDU AI: CORE INFERENCE COMPLETE")
    print("="*50)
    print(f"Detected Subtype: {analysis['estimated_subtype']}")
    print(f"Risk Score:       {analysis['risk_score']}")
    print(f"6-Month Trend:    {trend_status}")
    print("\nGENERATED LLM PROMPT:")
    print("-" * 30)
    print(final_prompt)
    print("-" * 30)

if __name__ == "__main__":
    run_heal_edu_core()