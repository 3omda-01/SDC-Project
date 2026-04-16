import httpx
import json
from typing import Optional, Dict, Any

OLLAMA_HOST = "http://localhost:11434"
DEFAULT_MODEL = "llama3.2"


class LLMClient:
    def __init__(self, host: str = OLLAMA_HOST, model: str = DEFAULT_MODEL):
        self.host = host
        self.model = model
        self.client = httpx.Client(timeout=120.0)
    
    def is_available(self) -> bool:
        try:
            response = self.client.get(f"{self.host}/api/tags")
            return response.status_code == 200
        except:
            return False
    
    def list_models(self) -> list:
        try:
            response = self.client.get(f"{self.host}/api/tags")
            if response.status_code == 200:
                data = response.json()
                return [m["name"] for m in data.get("models", [])]
            return []
        except:
            return []
    
    def generate(
        self,
        prompt: str,
        model: Optional[str] = None,
        system: Optional[str] = None,
        temperature: float = 0.7,
        max_tokens: int = 500,
        stream: bool = False
    ) -> Dict[str, Any]:
        model = model or self.model
        
        payload = {
            "model": model,
            "prompt": prompt,
            "stream": stream,
            "options": {
                "temperature": temperature,
                "num_predict": max_tokens,
            }
        }
        
        if system:
            payload["system"] = system
        
        try:
            response = self.client.post(
                f"{self.host}/api/generate",
                json=payload
            )
            
            if response.status_code == 200:
                return response.json()
            else:
                return {"error": f"HTTP {response.status_code}", "raw": response.text}
                
        except Exception as e:
            return {"error": str(e)}
    
    def chat(
        self,
        messages: list,
        model: Optional[str] = None,
        temperature: float = 0.7,
        max_tokens: int = 500
    ) -> Dict[str, Any]:
        model = model or self.model
        
        payload = {
            "model": model,
            "messages": messages,
            "stream": False,
            "options": {
                "temperature": temperature,
                "num_predict": max_tokens,
            }
        }
        
        try:
            response = self.client.post(
                f"{self.host}/api/chat",
                json=payload
            )
            
            if response.status_code == 200:
                return response.json()
            else:
                return {"error": f"HTTP {response.status_code}"}
                
        except Exception as e:
            return {"error": str(e)}
    
    def generate_feedback(self, prompt: str) -> str:
        result = self.generate(
            prompt=prompt,
            system="You are HealEdu AI, a supportive assistant for parents of children with attention difficulties. Be empathetic, clear, and non-alarmist.",
            temperature=0.7,
            max_tokens=600
        )
        
        if "error" in result:
            return f"LLM Error: {result['error']}"
        
        return result.get("response", "No response generated")


def check_ollama():
    client = LLMClient()
    
    print("Checking Ollama status...")
    
    if client.is_available():
        print("✓ Ollama is running")
        models = client.list_models()
        print(f"✓ Available models: {models if models else 'None'}")
        return True
    else:
        print("✗ Ollama is not running")
        print("  Install: curl -fsSL https://ollama.com/install.sh | sh")
        print("  Start:   ollama serve")
        print("  Pull model: ollama pull llama3.2")
        return False


if __name__ == "__main__":
    check_ollama()
