import pandas as pd
import numpy as np
from datetime import datetime, timedelta

def generate_six_month_data(student_id="STUDENT_001"):
    """Generates 180 days of synthetic ADHD screening data."""
    start_date = datetime.now() - timedelta(days=180)
    data_records = []

    for i in range(180):
        current_date = start_date + timedelta(days=i)
        
        # Simulate a 20-minute session [cite: 90, 103]
        record = {
            "date": current_date.strftime("%Y-%m-%d"),
            "student_id": student_id,
            "omission_errors": np.random.randint(5, 15), # Higher for Inattentive [cite: 108]
            "commission_errors": np.random.randint(1, 5),
            "mean_rt": round(np.random.uniform(0.5, 0.8), 3),
            "avg_hr": np.random.randint(70, 110), # Heart Rate [cite: 46, 60]
            "stress_events": np.random.randint(0, 3) # Detected by AI model [cite: 56]
        }
        data_records.append(record)

    df = pd.DataFrame(data_records)
    df.to_csv("data/longitudinal_sample.csv", index=False)
    print(f"Generated 180 days of data in data/longitudinal_sample.csv")

if __name__ == "__main__":
    generate_six_month_data()