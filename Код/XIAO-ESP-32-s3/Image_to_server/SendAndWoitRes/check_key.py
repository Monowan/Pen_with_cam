# Перевірка ключа Gemini: чи живий, чи є доступ до моделей, які працюють
import json
import sys
import urllib.error
import urllib.request

KEY = sys.argv[1] if len(sys.argv) > 1 else "AIzaSyBLxtfJNp62twqN0AipAizeB1Uyc0fFA7Q"
BASE = "https://generativelanguage.googleapis.com/v1beta"


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=25) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:500]
    except Exception as e:
        return 0, f"{type(e).__name__}: {e}"


print(f"1) список моделей: {BASE}/models")
code, data = get(f"{BASE}/models?key={KEY}")
if code != 200:
    print(f"   ✗ {code}: {data}")
    print("\n   -> ключ не має доступу до Generative Language API")
    sys.exit(1)

models = [m["name"].split("/")[-1] for m in data.get("models", [])
          if "generateContent" in m.get("supportedGenerationMethods", [])]
print(f"   ✓ {len(models)} моделей з generateContent")
print("   доступні flash/2.5:", [m for m in models if "flash" in m][:10])

# 2) реальний запит до кожної моделі
model = next((m for m in ["gemini-2.5-flash", "gemini-2.0-flash", "gemini-flash-latest"] if m in models), None)
if not model:
    print("\n   ✗ немає жодної придатної flash-моделі")
    sys.exit(1)

print(f"\n2) тестовий запит до {model}")
body = json.dumps({
    "contents": [{"parts": [{"text": "Answer with a single digit: what is 2+3?"}]}],
    "generationConfig": {"temperature": 0, "maxOutputTokens": 8},
}).encode()
req = urllib.request.Request(
    f"{BASE}/models/{model}:generateContent?key={KEY}",
    data=body, headers={"Content-Type": "application/json"}, method="POST")
try:
    with urllib.request.urlopen(req, timeout=40) as r:
        d = json.loads(r.read().decode())
        print("   ✓", d["candidates"][0]["content"]["parts"][0]["text"].strip())
except urllib.error.HTTPError as e:
    print(f"   ✗ {e.code}: {e.read().decode('utf-8', 'replace')[:400]}")
except Exception as e:
    print(f"   ✗ {type(e).__name__}: {e}")
