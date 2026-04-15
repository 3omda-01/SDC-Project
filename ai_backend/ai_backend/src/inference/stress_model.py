import numpy as np
from sklearn.ensemble import RandomForestClassifier
import joblib  # This MUST be joblib, not jobpath
import os

class StressModel:
    def __init__(self, model_path="models/stress_classifier.joblib"):
        self.model_path = model_path
        self.clf = RandomForestClassifier(n_estimators=100)
        
        # Ensure the models directory exists
        if not os.path.exists('models'):
            os.makedirs('models')

    def train_mock_model(self):
        """Trains the model on synthetic physiological data."""
        # Features: [Avg_HR, HRV_Index, Respiration_Rate]
        X = np.array([
            [75, 50, 16], [80, 55, 15],  # Calm
            [115, 20, 25], [120, 15, 28], # Stressed
            [95, 30, 20], [100, 25, 22]   # Fatigued
        ])
        # Labels: 0=Calm, 1=Stressed, 2=Fatigued
        y = np.array([0, 0, 1, 1, 2, 2])
        
        self.clf.fit(X, y)
        # Save the model
        joblib.dump(self.clf, self.model_path)
        print("Stress Detection Model trained and saved successfully.")

    def predict_state(self, hr, hrv, resp):
        """Predicts the student's physiological state."""
        state_map = {0: "Calm", 1: "Stressed", 2: "Fatigued"}
        try:
            prediction = self.clf.predict([[hr, hrv, resp]])[0]
            return state_map[prediction]
        except:
            return "Normal"

if __name__ == "__main__":
    model = StressModel()
    model.train_mock_model()
    result = model.predict_state(118, 18, 26)
    print(f"Test Prediction: {result}")