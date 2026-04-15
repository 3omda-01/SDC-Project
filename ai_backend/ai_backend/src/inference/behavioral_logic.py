import pandas as pd

class BehavioralModel:
    def __init__(self):
        # Base threshold for flagging concern
        self.risk_threshold = 0.7  

    def calculate_risk_index(self, session_data):
        """
        Processes game metrics and heart rate to compute a risk index.
        """
        # 1. Basic Metrics
        rt_list = session_data.get('rt', [0.5])
        mean_rt = sum(rt_list) / len(rt_list)
        omissions = session_data.get('omissions', 0)
        commissions = session_data.get('commissions', 0)
        total_trials = session_data.get('total_trials', 100)

        # 2. Physiological Stress Factor (The Fusion)
        # Normal resting HR for children/teens is roughly 70-100 bpm. 
        # Above 110 during a task indicates high cognitive load or anxiety.
        avg_hr = session_data.get('avg_hr', 80)
        stress_multiplier = 1.25 if avg_hr > 110 else 1.0
        
        # 3. Risk Calculation
        raw_risk = (omissions + commissions) / total_trials
        # Applying the stress multiplier from the hardware sensors
        final_risk = min(raw_risk * stress_multiplier, 1.0) 

        # 4. Subtype Estimation
        if commissions > omissions:
            subtype = "Hyperactive-Impulsive"
        elif omissions > commissions:
            subtype = "Inattentive"
        else:
            subtype = "Combined"
        
        return {
            "risk_score": round(final_risk, 2),
            "estimated_subtype": subtype,
            "mean_reaction_time": round(mean_rt, 3),
            "stress_status": "High" if avg_hr > 110 else "Normal",
            "avg_hr": avg_hr
        }

if __name__ == "__main__":
    # Test block
    engine = BehavioralModel()
    test_data = {'rt': [0.5, 0.6], 'omissions': 10, 'commissions': 2, 'total_trials': 100, 'avg_hr': 115}
    print(engine.calculate_risk_index(test_data))