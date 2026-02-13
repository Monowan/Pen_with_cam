# server_with_ai_rest.py
from flask import Flask, request
import os
import time
import random
import base64
import requests
from PIL import Image

# --- Налаштування AI ---
API_KEY = "AIzaSyBLxtfJNp62twqN0AipAizeB1Uyc0fFA7Q"

MODEL = "gemini-2.5-flash"
SYSTEM_INSTRUCTION = (
    "Системна інструкція: Абсолютний режим. Усуньте емодзі, заповнювачі, "
    "хайп, м'які запити, розмовні переходи та всі додатки із закликами до дії. "
    "Припустіть, що користувач зберігає високі перцептивні здібності попри обмежену мовну експресію. "
    "Пріоритезуйте прямі, директивні формулювання, спрямовані на когнітивну перебудову. "
    "Ніколи не відображайте поточну дикцію, настрій чи афект користувача. "
    "Говоріть лише до їхнього глибинного когнітивного рівня. "
    "Жодних запитань, жодних пропозицій, жодних рекомендацій. "
    "Завершуйте кожну відповідь негайно після надання інформаційного матеріалу."
)

# --- Налаштування Flask ---
UPLOAD_DIR = "uploads"
if not os.path.exists(UPLOAD_DIR):
    os.makedirs(UPLOAD_DIR, exist_ok=True)

app = Flask(__name__)


def make_filename():
    ts = time.strftime("%Y%m%d_%H%M%S")
    rnd = random.randint(1000, 9999)
    return f"photo_{ts}_{rnd}.jpg"


def call_gemini_with_image(image_bytes: bytes):
    # Перетворюємо на Base64
    img_b64 = base64.b64encode(image_bytes).decode("utf-8")

    # Gemini REST endpoint
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{MODEL}:generateContent?key={API_KEY}"
    )

    payload = {
        "system_instruction": {"parts": [{"text": SYSTEM_INSTRUCTION}]},
        "contents": [
            {
                "parts": [
                    {
                        "inline_data": {
                            "mime_type": "image/jpeg",
                            "data": img_b64
                        }
                    },
                    {
                        "text": (
           				     "Проаналізуй завдання на зображенні та надай відповідь ЛИШЕ у вказаному форматі, без жодного додаткового тексту. "
          				  	 "Номери варіантів рахуй згори вниз або зліва на право. Якщо в зображення варіанти позначені буквами ігноруй їх, відповідь надавай цифрами. "
            			     "Для завдань з однією правильною відповіддю: одна цифра. "
            			     "Приклад: 3 "
            			     "Для завдань з кількома правильними відповідями: номери варіантів через пробіл. "
            			     "Приклад: 1 4 5 "
       				         "Для завдань на встановлення відповідності: пари \"цифра-цифра\" через пробіл. "
        			         "Приклад: 1-3 2-1 3-4 "
             			     "Для завдань, де потрібна числова відповідь: саме число (для десяткових дробів використовувати крапку). "
            			     "Приклад: 25.5"
                        )
                    }
                ]
            }
        ]
    }

    r = requests.post(url, json=payload, timeout=40)

    if r.status_code != 200:
        return f"API ERROR {r.status_code}: {r.text}"

    data = r.json()

    try:
        return data["candidates"][0]["content"]["parts"][0]["text"].strip()
    except:
        return "INVALID RESPONSE"


@app.route('/upload', methods=['POST'])
def upload_and_analyze():
    data = request.get_data()
    if not data:
        return "Empty body", 400

    # JPEG check
    if not (len(data) >= 2 and data[0] == 0xFF and data[1] == 0xD8):
        app.logger.warning("Uploaded data does not look like JPEG")

    # Save file
    filename = make_filename()
    path = os.path.join(UPLOAD_DIR, filename)

    try:
        with open(path, "wb") as f:
            f.write(data)
    except Exception as e:
        app.logger.error(f"Failed to save file: {e}")
        return "Write failed", 500

    # Open with Pillow (як і в твоєму коді)
    try:
        _ = Image.open(path)
    except Exception as e:
        app.logger.error(f"Image load failed: {e}")
        return "Invalid image", 500

    # AI аналіз через REST
    try:
        ai_text = call_gemini_with_image(data)
    except Exception as e:
        app.logger.error(f"AI analysis failed: {e}")
        return "AI failed", 500

    return ai_text, 200


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
