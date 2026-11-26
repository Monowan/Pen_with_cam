# server_ai.py — Flask-сервер для ручки з камерою (XIAO ESP32-S3 Sense)
#
# Приймає JPEG по HTTP POST /upload, зберігає у uploads/ і просить Gemini
# прочитати завдання тесту й повернути відповідь у суворому форматі.
#
# Запуск:
#   python3 -m venv .venv && source .venv/bin/activate
#   pip install -r requirements.txt
#   python server.py
#
# API-ключ: змінна середовища GEMINI_API_KEY, або api_key.txt поруч зі скриптом,
# або прямий рядок нижче (запасний варіант для швидкого старту).
import os
import sys
import time
import random
import base64
import json
import urllib.request
import urllib.error

from flask import Flask, request

# --- Залежності (Pillow — необов'язково) ---
try:
    from PIL import Image  # noqa: F401
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False

# ---------------------------------------------------------------------------
# Налаштування
# ---------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
UPLOAD_DIR = os.path.join(HERE, "uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)

FALLBACK_KEY = "AIzaSyBLxtfJNp62twqN0AipAizeB1Uyc0fFA7Q"


def load_api_key() -> str:
    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if key:
        return key
    p = os.path.join(HERE, "api_key.txt")
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            return f.read().strip()
    return FALLBACK_KEY


API_KEY = load_api_key()

# Порядок: перша модель, яка відповість 200, використовується далі
MODEL_CANDIDATES = [
    "gemini-2.5-flash",
    "gemini-2.0-flash",
    "gemini-flash-latest",
    "gemini-2.5-flash-lite",
]
_current_model = {"n": 0}

SYSTEM_INSTRUCTION = (
    "Ти — розв'язувач завдань українського НМТ. "
    "Відповідай ВИКЛЮЧНО у форматі, вказаному користувачем. "
    "Без пояснень, без емодзі, без запитань, без списків окрім самого формату."
)

PROMPT = (
    "Проаналізуй завдання на зображенні та надай відповідь ЛИШЕ у вказаному форматі, без жодного додаткового тексту. "
    "Номери варіантів рахуй згори вниз або зліва направо. Якщо варіанти позначені буквами — ігноруй їх, відповідай цифрами. "
    "Одна правильна відповідь: одна цифра. Приклад: 3. "
    "Кілька правильних відповідей: номери через пробіл. Приклад: 1 4 5. "
    "Встановлення відповідності: пари \"цифра-цифра\" через пробіл. Приклад: 1-3 2-1 3-4. "
    "Числова відповідь: саме число, десятковий розділювач — крапка. Приклад: 25.5"
)

app = Flask(__name__)


# ---------------------------------------------------------------------------
# Gemini REST
# ---------------------------------------------------------------------------
def call_gemini_once(model: str, image_bytes: bytes, mime: str = "image/jpeg"):
    """Повертає (status_code, parsed_or_raw)."""
    url = (
        "https://generativelanguage.googleapis.com/v1beta/models/"
        f"{model}:generateContent?key={API_KEY}"
    )
    payload = {
        "system_instruction": {"parts": [{"text": SYSTEM_INSTRUCTION}]},
        "contents": [{
            "role": "user",
            "parts": [
                {"inline_data": {"mime_type": mime,
                                 "data": base64.b64encode(image_bytes).decode("utf-8")}},
                {"text": PROMPT},
            ],
        }],
        "generationConfig": {"temperature": 0, "maxOutputTokens": 64},
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=45) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        return e.code, body
    except Exception as e:  # таймаут, DNS, відсутність інтернету
        return 0, f"{type(e).__name__}: {e}"


def call_gemini(image_bytes: bytes, mime: str = "image/jpeg"):
    """Пробує моделі по колу, поки одна не дасть 200."""
    order = MODEL_CANDIDATES[_current_model["n"]:] + MODEL_CANDIDATES[:_current_model["n"]]
    errors = []
    for model in order:
        code, data = call_gemini_once(model, image_bytes, mime)
        if code == 200:
            try:
                out = data["candidates"][0]["content"]["parts"][0]["text"].strip()
            except Exception:
                out = "INVALID RESPONSE"
            _current_model["n"] = MODEL_CANDIDATES.index(model)
            app.logger.info("model=%s ok -> %s", model, out)
            return out
        errors.append(f"{model}: {code} {str(data)[:200]}")
        app.logger.warning("model=%s failed: %s", model, str(data)[:200])
    return "AI ERROR\n" + "\n".join(errors)


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
def make_filename():
    return f"photo_{time.strftime('%Y%m%d_%H%M%S')}_{random.randint(1000, 9999)}.jpg"


@app.route("/", methods=["GET"])
def index():
    return "ok", 200


@app.route("/health", methods=["GET"])
def health():
    return {
        "status": "ok",
        "pillow": HAVE_PIL,
        "models_tried_first": MODEL_CANDIDATES[_current_model["n"]],
        "uploads": UPLOAD_DIR,
    }


@app.route("/upload", methods=["POST"])
def upload_and_analyze():
    data = request.get_data()
    if not data:
        return "Empty body", 400

    if not (len(data) >= 2 and data[0] == 0xFF and data[1] == 0xD8):
        app.logger.warning("body does not start with JPEG SOI (0xFFD8)")
        return "Not a JPEG", 400

    path = os.path.join(UPLOAD_DIR, make_filename())
    try:
        with open(path, "wb") as f:
            f.write(data)
    except Exception as e:
        app.logger.error("write failed: %s", e)
        return "Write failed", 500

    if HAVE_PIL:
        try:
            from PIL import Image
            with Image.open(path) as im:
                im.verify()
        except Exception as e:
            app.logger.error("invalid image: %s", e)
            return "Invalid image", 400

    t0 = time.time()
    answer = call_gemini(data)
    app.logger.info("upload %s bytes=%d in %.1fs -> %s",
                    os.path.basename(path), len(data), time.time() - t0, answer[:80])
    return answer, 200


@app.route("/test", methods=["POST"])
def test_with_local_file():
    """Для перевірки без ручки: POST з тілом JPEG або локальний файл з ?file=."""
    p = request.args.get("file")
    if p and os.path.exists(p):
        with open(p, "rb") as f:
            img = f.read()
    else:
        img = request.get_data()
    if not img:
        return "no image", 400
    return call_gemini(img), 200


if __name__ == "__main__":
    if not HAVE_PIL:
        print("[!] Pillow не встановлено — пропускаю перевірку цілісності зображень")
    print(f"[i] UPLOAD_DIR = {UPLOAD_DIR}")
    print(f"[i] ключ: {'з env' if os.environ.get('GEMINI_API_KEY') else ('з api_key.txt' if os.path.exists(os.path.join(HERE, 'api_key.txt')) else 'fallback у коді')}")
    print("[i] слухаю 0.0.0.0:5000")
    # Для локальної мережі з ручки: не включати reloader/debug
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
