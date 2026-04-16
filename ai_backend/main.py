import pandas as pd
import os
import asyncio
import struct
import time
from src.inference.behavioral_logic import BehavioralModel
from src.llm_handler.prompt_engine import get_parent_feedback_prompt
from src.llm_handler.llm_client import LLMClient, check_ollama
from src.ble_client import BLEClient, SensorReading


# BLE Commands
CMD_VIBRATE = 0x01
CMD_STOP_VIBRATE = 0x02
CMD_REQUEST_DATA = 0x03
CMD_SET_INTENSITY = 0x04
CMD_SAVE_SESSION = 0x05
CMD_RESET_MODEL = 0x06

# BLE Config Commands
CFG_ALERTS = 0x10
CFG_CALIBRATE = 0x11
CFG_MODE = 0x12
CFG_STATUS = 0x13


class HealEduAI:
    def __init__(self, use_llm: bool = True):
        self.engine = BehavioralModel()
        self.ble_client = BLEClient()
        self.llm_client = LLMClient() if use_llm else None
        self.use_llm = use_llm
        self.current_reading: SensorReading = None
        self.attention_history = []
        self.session_data = []
        
    def on_sensor_data(self, reading: SensorReading):
        self.current_reading = reading
        self.session_data.append({
            'timestamp': time.time(),
            'hr': reading.heart_rate,
            'spo2': reading.spO2,
            'motion': reading.motion,
            'attention': getattr(reading, 'attention', 0.8),
            'stress': getattr(reading, 'stress', 0),
        })
        self.attention_history.append(getattr(reading, 'attention', 0.8))
        
        attention_pct = getattr(reading, 'attention', 0.8) * 100
        state_labels = ['LOW', 'MEDIUM', 'HIGH']
        state = getattr(reading, 'attention_state', 2)
        
        print(f"[BLE Data] HR: {reading.heart_rate:.0f} | SpO2: {reading.spO2:.0f}% | "
              f"Motion: {reading.motion:.2f}g | Attn: {attention_pct:.0f}% ({state_labels[state]}) | "
              f"Bat: {reading.battery_percent}%")
    
    async def connect(self) -> bool:
        print("Connecting to HealEdu device via BLE...")
        return await self.ble_client.connect()
    
    async def disconnect(self):
        await self.ble_client.disconnect()
    
    async def enable_attention_alerts(self, enabled: bool = True):
        config = bytes([CFG_ALERTS, 1 if enabled else 0])
        print(f"Enabling attention alerts: {enabled}")
    
    async def calibrate_model(self):
        print("Calibrating attention model...")
        config = bytes([CFG_CALIBRATE])
    
    async def set_mode(self, ble_only: bool = True):
        print(f"Setting mode: {'BLE-only' if ble_only else 'WiFi+BLE'}")
        config = bytes([CFG_MODE, 1 if ble_only else 0])
    
    async def get_device_status(self):
        config = bytes([CFG_STATUS])
        print("Requesting device status...")
    
    async def trigger_vibration(self):
        await self.ble_client.vibrate()
        print("Triggered vibration")
    
    async def set_vibration_intensity(self, intensity: int):
        await self.ble_client.set_intensity(intensity)
        print(f"Set vibration intensity: {intensity}")
    
    async def run_calibration(self, duration_seconds: int = 30):
        print(f"\n=== Calibration Phase ({duration_seconds}s) ===")
        print("Please stay calm and avoid movement...")
        
        await self.calibrate_model()
        
        start_time = time.time()
        cal_data = []
        
        while time.time() - start_time < duration_seconds:
            await asyncio.sleep(0.5)
            if self.current_reading:
                cal_data.append({
                    'hr': self.current_reading.heart_rate,
                    'motion': self.current_reading.motion,
                    'spo2': self.current_reading.spO2
                })
        
        if cal_data:
            avg_hr = sum(d['hr'] for d in cal_data) / len(cal_data)
            avg_motion = sum(d['motion'] for d in cal_data) / len(cal_data)
            print(f"\nCalibration complete:")
            print(f"  Average HR: {avg_hr:.1f} bpm")
            print(f"  Average Motion: {avg_motion:.3f}g")
    
    async def run_session(self, duration_seconds: int = 60):
        print(f"\n=== Running Session ({duration_seconds}s) ===")
        
        self.session_data = []
        self.attention_history = []
        
        start_time = time.time()
        
        while time.time() - start_time < duration_seconds:
            await asyncio.sleep(0.5)
        
        return self.generate_session_report()
    
    def generate_session_report(self):
        if not self.session_data:
            return None
        
        avg_attention = sum(self.attention_history) / len(self.attention_history) if self.attention_history else 0
        low_attention_count = sum(1 for a in self.attention_history if a < 0.4)
        low_attention_pct = (low_attention_count / len(self.attention_history) * 100) if self.attention_history else 0
        
        avg_hr = sum(d['hr'] for d in self.session_data if d['hr'] > 0) / len([d for d in self.session_data if d['hr'] > 0]) if any(d['hr'] > 0 for d in self.session_data) else 0
        avg_motion = sum(d['motion'] for d in self.session_data) / len(self.session_data)
        
        session_analysis = {
            'avg_attention': avg_attention,
            'attention_states': {
                'low': sum(1 for a in self.attention_history if a < 0.4),
                'medium': sum(1 for a in self.attention_history if 0.4 <= a < 0.7),
                'high': sum(1 for a in self.attention_history if a >= 0.7),
            },
            'low_attention_pct': low_attention_pct,
            'avg_heart_rate': avg_hr,
            'avg_motion': avg_motion,
            'total_samples': len(self.session_data),
        }
        
        current_session = {
            'rt': [0.5],
            'omissions': int(low_attention_pct),
            'commissions': int(avg_motion < 0.1),
            'total_trials': len(self.session_data),
            'avg_hr': avg_hr,
        }
        
        ml_analysis = self.engine.calculate_risk_index(current_session)
        
        trend_status = "Improving" if avg_attention >= 0.7 else "Needs Attention"
        if avg_attention >= 0.8:
            trend_status = "Excellent"
        elif avg_attention >= 0.6:
            trend_status = "Good"
        elif avg_attention >= 0.4:
            trend_status = "Needs Improvement"
        else:
            trend_status = "Critical - Immediate Action"
        
        report = {
            'session': session_analysis,
            'ml_analysis': ml_analysis,
            'trend_status': trend_status,
        }
        
        if self.use_llm and self.llm_client and self.llm_client.is_available():
            prompt = self.get_llm_feedback(report)
            llm_response = self.llm_client.generate_feedback(prompt)
            report['llm_feedback'] = llm_response
            report['llm_available'] = True
        else:
            report['llm_feedback'] = self.get_llm_feedback(report)
            report['llm_available'] = False
        
        return report
    
    async def run_full_session(self, calibration_duration: int = 30, session_duration: int = 60):
        print("=" * 60)
        print("HealEdu AI - Full Session with Edge AI + Cloud ML")
        print("=" * 60)
        
        if not await self.connect():
            print("Failed to connect to device")
            return None
        
        await self.calibrate_model()
        await asyncio.sleep(2)
        
        await self.run_calibration(calibration_duration)
        
        report = await self.run_session(session_duration)
        
        if report:
            print("\n" + "=" * 60)
            print("SESSION REPORT")
            print("=" * 60)
            print(f"Average Attention: {report['session']['avg_attention']*100:.1f}%")
            print(f"Attention States: HIGH={report['session']['attention_states']['high']}, "
                  f"MEDIUM={report['session']['attention_states']['medium']}, "
                  f"LOW={report['session']['attention_states']['low']}")
            print(f"Average Heart Rate: {report['session']['avg_heart_rate']:.1f} bpm")
            print(f"Average Motion: {report['session']['avg_motion']:.3f}g")
            print("-" * 60)
            print(f"ML Risk Score: {report['ml_analysis']['risk_score']}")
            print(f"Estimated Subtype: {report['ml_analysis']['estimated_subtype']}")
            print(f"Overall Status: {report['trend_status']}")
            
            if report.get('llm_available'):
                print("-" * 60)
                print("LLM FEEDBACK (via Ollama):")
                print(report['llm_feedback'])
            
            print("=" * 60)
        
        await self.disconnect()
        return report
    
    def get_llm_feedback(self, report):
        if not report:
            return None
        
        return get_parent_feedback_prompt(
            risk_index=report['ml_analysis']['risk_score'],
            subtype=report['ml_analysis']['estimated_subtype'],
            trends=report['trend_status']
        )


async def demo_mode():
    print("=" * 60)
    print("HealEdu AI - Demo Mode (No BLE Device Required)")
    print("=" * 60)
    
    atteio = HealEduAI(use_llm=True)
    
    demo_readings = [
        SensorReading(heart_rate=85, spO2=98, motion=0.05, attention=0.85),
        SensorReading(heart_rate=92, spO2=97, motion=0.15, attention=0.72),
        SensorReading(heart_rate=88, spO2=98, motion=0.02, attention=0.90),
        SensorReading(heart_rate=105, spO2=96, motion=0.45, attention=0.35),
        SensorReading(heart_rate=95, spO2=97, motion=0.08, attention=0.65),
    ]
    
    for reading in demo_readings:
        atteio.on_sensor_data(reading)
        await asyncio.sleep(0.5)
    
    session_data = atteio.session_data
    attention_history = atteio.attention_history
    
    if attention_history:
        avg_attention = sum(attention_history) / len(attention_history)
        
        ml_analysis = atteio.engine.calculate_risk_index({
            'rt': [0.5],
            'omissions': sum(1 for a in attention_history if a < 0.4),
            'commissions': sum(1 for a in attention_history if a > 0.8),
            'total_trials': len(attention_history),
            'avg_hr': 90,
        })
        
        trend = "Good" if avg_attention > 0.7 else "Needs Attention"
        
        report = {
            'session': {'avg_attention': avg_attention},
            'ml_analysis': ml_analysis,
            'trend_status': trend,
        }
        
        if atteio.use_llm and atteio.llm_client and atteio.llm_client.is_available():
            prompt = atteio.get_llm_feedback(report)
            llm_response = atteio.llm_client.generate_feedback(prompt)
            print("\n" + "=" * 60)
            print("DEMO SESSION REPORT (with Ollama LLM)")
            print("=" * 60)
            print(f"Average Attention: {avg_attention*100:.1f}%")
            print(f"ML Risk Score: {ml_analysis['risk_score']}")
            print(f"Estimated Subtype: {ml_analysis['estimated_subtype']}")
            print("-" * 60)
            print("LLM FEEDBACK:")
            print(llm_response)
        else:
            print("\n" + "=" * 60)
            print("DEMO SESSION REPORT")
            print("=" * 60)
            print(f"Average Attention: {avg_attention*100:.1f}%")
            print(f"ML Risk Score: {ml_analysis['risk_score']}")
            print(f"Estimated Subtype: {ml_analysis['estimated_subtype']}")
            
            final_prompt = get_parent_feedback_prompt(
                risk_index=ml_analysis['risk_score'],
                subtype=ml_analysis['estimated_subtype'],
                trends=trend
            )
            print("\nGENERATED LLM PROMPT:")
            print("-" * 40)
            print(final_prompt)
            print("\n(Start Ollama to get AI-generated feedback)")
        
        print("=" * 60)


def run_heal_edu_core():
    data_path = "data/longitudinal_sample.csv"
    if not os.path.exists(data_path):
        print("Error: Data file not found. Running demo mode...")
        asyncio.run(demo_mode())
        return

    df = pd.read_csv(data_path)
    
    last_row = df.iloc[-1]
    current_session = {
        'rt': [last_row['mean_rt']], 
        'omissions': last_row['omission_errors'],
        'commissions': last_row['commission_errors'],
        'total_trials': 100 
    }
    
    engine = BehavioralModel()
    analysis = engine.calculate_risk_index(current_session)
    
    avg_omissions = df['omission_errors'].mean()
    trend_status = "Improving" if last_row['omission_errors'] < avg_omissions else "Attention Required"

    final_prompt = get_parent_feedback_prompt(
        risk_index=analysis['risk_score'],
        subtype=analysis['estimated_subtype'],
        trends=trend_status
    )

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
    import sys
    if len(sys.argv) > 1:
        if sys.argv[1] == "--ble":
            atteio = HealEduAI()
            asyncio.run(atteio.run_full_session())
        elif sys.argv[1] == "--demo":
            asyncio.run(demo_mode())
        elif sys.argv[1] == "--calibrate":
            atteio = HealEduAI()
            asyncio.run(atteio.connect())
            asyncio.run(atteio.run_calibration(30))
            asyncio.run(atteio.disconnect())
        else:
            print("Usage: python main.py [--ble|--demo|--calibrate]")
    else:
        run_heal_edu_core()
