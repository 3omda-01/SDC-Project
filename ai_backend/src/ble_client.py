import asyncio
import json
import struct
from typing import Optional, Callable, Dict, Any
from dataclasses import dataclass, field
import bleak

BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
BLE_CHAR_SENSOR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLE_CHAR_COMMAND_UUID = "beb5483e-36e1-4688-b7f6-ea07361b26a8"
BLE_DEVICE_NAME = "HealEdu_MVP"

CMD_VIBRATE = 0x01
CMD_STOP_VIBRATE = 0x02
CMD_REQUEST_DATA = 0x03
CMD_SET_INTENSITY = 0x04


@dataclass
class SensorReading:
    heart_rate: float = 0
    spO2: float = 0
    motion: float = 0
    avg_heart_rate: float = 0
    hr_samples: int = 0
    battery_percent: int = 0
    battery_voltage: float = 0
    attention: float = 1.0
    attention_state: int = 2
    stress: float = 0
    accel_x: float = 0
    accel_y: float = 0
    accel_z: float = 0
    gyro_x: float = 0
    gyro_y: float = 0
    gyro_z: float = 0
    timestamp: int = 0
    
    @classmethod
    def from_json(cls, data: Dict[str, Any]) -> "SensorReading":
        return cls(
            heart_rate=data.get("hr", 0),
            spO2=data.get("spo2", 0),
            motion=data.get("motion", 0),
            avg_heart_rate=data.get("avg_hr", 0),
            hr_samples=data.get("hr_n", 0),
            battery_percent=data.get("bat_pct", 0),
            battery_voltage=data.get("bat_v", 0),
            attention=data.get("attn", 1.0),
            attention_state=data.get("attn_state", 2),
            stress=data.get("stress", 0),
            accel_x=data.get("ax", 0),
            accel_y=data.get("ay", 0),
            accel_z=data.get("az", 0),
            gyro_x=data.get("gx", 0),
            gyro_y=data.get("gy", 0),
            gyro_z=data.get("gz", 0),
            timestamp=data.get("ts", 0)
        )


class BLEClient:
    def __init__(
        self,
        device_name: str = BLE_DEVICE_NAME,
        on_data_callback: Optional[Callable[[SensorReading], None]] = None
    ):
        self.device_name = device_name
        self.on_data_callback = on_data_callback
        self.client: Optional[bleak.BleakClient] = None
        self.connected = False
        self._sensor_char: Optional[bleak.BleakGATTCharacteristic] = None
        self._command_char: Optional[bleak.BleakGATTCharacteristic] = None
        
    async def connect(self, timeout: float = 10.0) -> bool:
        try:
            devices = await bleak.BleakScanner.discover(timeout=timeout)
            
            target_device = None
            for d in devices:
                if d.name and BLE_DEVICE_NAME in d.name:
                    target_device = d
                    break
            
            if not target_device:
                print(f"BLE: Device '{self.device_name}' not found")
                return False
            
            self.client = bleak.BleakClient(target_device, timeout=timeout)
            await self.client.connect()
            
            services = self.client.services
            for service in services:
                if service.uuid.lower() == BLE_SERVICE_UUID.lower():
                    for char in service.characteristics:
                        if char.uuid.lower() == BLE_CHAR_SENSOR_UUID.lower():
                            self._sensor_char = char
                        elif char.uuid.lower() == BLE_CHAR_COMMAND_UUID.lower():
                            self._command_char = char
            
            if self._sensor_char:
                await self.client.start_notify(
                    self._sensor_char.handle,
                    self._handle_sensor_data
                )
            
            self.connected = True
            print(f"BLE: Connected to {target_device.name}")
            return True
            
        except Exception as e:
            print(f"BLE: Connection failed - {e}")
            self.connected = False
            return False
    
    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        self.connected = False
        print("BLE: Disconnected")
    
    def _handle_sensor_data(self, sender: int, data: bytearray):
        try:
            json_str = data.decode("utf-8")
            data_dict = json.loads(json_str)
            reading = SensorReading.from_json(data_dict)
            
            if self.on_data_callback:
                self.on_data_callback(reading)
                
        except Exception as e:
            print(f"BLE: Data parse error - {e}")
    
    async def send_command(self, command: int, payload: bytes = b"") -> bool:
        if not self.connected or not self._command_char:
            return False
        
        try:
            data = bytes([command]) + payload
            await self.client.write_gatt_char(
                self._command_char.handle,
                data,
                response=True
            )
            return True
        except Exception as e:
            print(f"BLE: Command send failed - {e}")
            return False
    
    async def vibrate(self, duration_ms: int = 200) -> bool:
        return await self.send_command(CMD_VIBRATE)
    
    async def stop_vibration(self) -> bool:
        return await self.send_command(CMD_STOP_VIBRATE)
    
    async def request_data(self) -> bool:
        return await self.send_command(CMD_REQUEST_DATA)
    
    async def set_intensity(self, intensity: int) -> bool:
        intensity = max(0, min(255, intensity))
        return await self.send_command(CMD_SET_INTENSITY, bytes([intensity]))
    
    async def run_continuous(
        self,
        on_data: Optional[Callable[[SensorReading], None]] = None,
        poll_interval: float = 0.5
    ):
        callback = on_data or self.on_data_callback
        while self.connected:
            await self.request_data()
            await asyncio.sleep(poll_interval)


async def example_usage():
    def on_data(reading: SensorReading):
        print(f"HR: {reading.heart_rate:.0f} bpm | SpO2: {reading.spO2:.0f}% | "
              f"Motion: {reading.motion:.2f}g | Battery: {reading.battery_percent}%")
    
    client = BLEClient(on_data_callback=on_data)
    
    if await client.connect():
        try:
            await asyncio.sleep(10)
        finally:
            await client.disconnect()


if __name__ == "__main__":
    asyncio.run(example_usage())
