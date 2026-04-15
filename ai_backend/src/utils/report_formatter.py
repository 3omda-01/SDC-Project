import pandas as pd
from datetime import datetime

class ReportFormatter:
    def __init__(self, data_path="data/longitudinal_sample.csv"):
        self.data_path = data_path

    def generate_summary(self, student_id):
        """Analyzes 6 months of data to produce a clinical summary."""
        try:
            df = pd.read_csv(self.data_path)
            student_df = df[df['student_id'] == student_id]
            
            if student_df.empty:
                return "No data found for this student."

            # Calculate Statistics
            initial_risk = (student_df.iloc[0]['omission_errors'] + student_df.iloc[0]['commission_errors']) / 100
            current_risk = (student_df.iloc[-1]['omission_errors'] + student_df.iloc[-1]['commission_errors']) / 100
            improvement = round((initial_risk - current_risk) * 100, 1)

            # Determine Subtype Consistency
            # If commissions > omissions more than 70% of the time
            impulsive_count = len(student_df[student_df['commission_errors'] > student_df['omission_errors']])
            consistency = "Hyperactive-Impulsive" if impulsive_count / len(student_df) > 0.7 else "Inattentive"

            report = f"""
            ==================================================
            HEALEDU CLINICAL PROGRESS REPORT
            ==================================================
            Student ID: {student_id}
            Date of Export: {datetime.now().strftime('%Y-%m-%d')}
            Tracking Period: 180 Days
            
            DIAGNOSTIC INDICATORS:
            - Primary Observation: {consistency}
            - Initial Risk Index: {round(initial_risk, 2)}
            - Current Risk Index: {round(current_risk, 2)}
            - Net Improvement: {improvement}%
            
            PHYSIOLOGICAL SUMMARY:
            - Avg Heart Rate (Task): {round(student_df['avg_hr'].mean(), 1)} BPM
            - Stress Events Detected: {student_df['stress_events'].sum()}
            
            ADAPTIVE STATUS:
            The student is showing {'positive' if improvement > 0 else 'stable'} 
            adaptation to the current behavioral strategies.
            ==================================================
            """
            return report
        except Exception as e:
            return f"Error generating report: {str(e)}"

if __name__ == "__main__":
    formatter = ReportFormatter()
    print(formatter.generate_summary("STUDENT_001"))