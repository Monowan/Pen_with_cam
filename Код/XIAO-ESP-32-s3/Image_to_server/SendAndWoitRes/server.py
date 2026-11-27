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
import logging
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

# Пріоритет джерел ключа (перший непорожній перемагає):
#   1) змінна середовища GEMINI_API_KEY
#   2) файл api_key.txt поруч зі скриптом (у .gitignore, git його не чіпає)
#   3) ~/.gemini_api_key (спільний для всіх клонів репозиторію)
# Ключа в коді НЕМАЄ навмисно: інакше git при кожному pull/merge
# повертав би старий закомічений рядок — саме те, чого треба уникнути.
KEY_FILES = [
    os.path.join(HERE, "api_key.txt"),
    os.path.expanduser("~/.gemini_api_key"),
]


def load_api_key() -> str:
    """Повертає (ключ, опис джерела)."""
    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if key:
        return key, "env GEMINI_API_KEY"
    for p in KEY_FILES:
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f:
                key = f.read().strip()
            if key:
                return key, p
    return "", "немає"


API_KEY, KEY_SOURCE = load_api_key()

# Порядок: перша модель, яка відповість 200, використовується далі.
# Перевірено реальними запитами 2026-11-27: працюють саме ці три.
# gemini-2.5-flash / 2.0-flash / 2.5-pro — 404 "no longer available to new users".
# gemini-3.8-flash / 3.7-flash — 503 high demand або таймаут, тримаємо в кінці.
MODEL_CANDIDATES = [
    "gemini-3.5-flash-lite",     # 200, стабільна
    "gemini-3.1-flash-lite",     # 200, стабільна
    "gemini-flash-lite-latest",  # 200, стабільна
    "gemini-3-flash-preview",    # 200 (v1beta)
    "gemini-3.6-flash",          # 200/503 залежно від навантаження
    "gemini-3.5-flash",          # 200/503 залежно від навантаження
]
_current_model = {"n": 0}

SYSTEM_INSTRUCTION = (
    "Ти — контролер якості розпізнавання тестових завдань НМТ. "
    "Твоє єдине завдання — оцінити, чи достатньо даних на фото для однозначної відповіді, "
    "і дати відповідь ВИКЛЮЧНО у форматі, вказаному користувачем. "
    "Без пояснень, без емодзі, без запитань."
)

# Маркери НЕВДАЧІ. Починаються з '!' і містять літери, тому фізично не можуть
# збігтися з будь-якою допустимою відповіддю (цифри, пробіли, дефіси, крапка).
ANSWER_OK = "ANSWER:"
ANSWER_FAIL = "!"       # будь-яка відповідь, що починається з '!', = не розпізнано

PROMPT = (
    "КРОК 1 — ЧИТАННЯ. Прочитай умову завдання та всі варіанти відповіді дослівно.\n"
    "КРОК 2 — ПЕРЕВІРКА. Відповідай на два питання:\n"
    "  а) Чи всі символи умови та всіх варіантів видно чітко і без двозначності?\n"
    "  б) Чи дає прочитаний текст ОДНУ однозначну відповідь?\n"
    "Якщо на будь-яке питання відповідь НІ — це НЕВДАЧА, див. нижче.\n"
    "КРОК 3 — ВІДПОВІДЬ. Лише якщо обидва НІ сказані 'так', розв'яжи завдання.\n\n"
    "НЕВДАЧА (починай відповідь дослівно з '!FAIL: ' і далі коротка причина українською):\n"
    "- фото розмите, затемнене, засвітлене, обрізане, перевернуте або текст не читається;\n"
    "- умови або варіантів немає в кадрі / їх видно частково;\n"
    "- на зображенні не тестове завдання (немає питання або варіантів);\n"
    "- хоча б один символ числа, формули чи варіанта прочитано невпевнено;\n"
    "- завдання має кілька можливих тлумачень або потрібні дані, яких немає на фото.\n"
    "Приклади: !FAIL: розмито, не видно умову    /    !FAIL: обрізано, немає варіантів\n\n"
    "КРИТИЧНІ ЗАБОРОНИ:\n"
    "- НЕ вгадуй, НЕ вибирай 'найімовірніший' варіант, НЕ розв'язуй частково прочитане завдання.\n"
    "- НЕ роби висновок 'фото нормальне' лише тому, що впізнав тему завдання.\n"
    "- Якщо є хоч найменший сумнів у прочитанні — це '!FAIL:'. Хибна відповідь гірша за відмову.\n"
    "- Ніколи не починай звичайну відповідь з '!' або з букв.\n\n"
    "Формат УСПІШНОЇ відповіді (нічого крім цього):\n"
    "Номери варіантів рахуй згори вниз або зліва направо. Якщо варіанти позначені буквами — ігноруй їх, відповідай цифрами. "
    "Одна правильна відповідь: одна цифра. Приклад: 3. "
    "Кілька правильних відповідей: номери через пробіл. Приклад: 1 4 5. "
    "Встановлення відповідності: пари \"цифра-цифра\" через пробіл. Приклад: 1-3 2-1 3-4. "
    "Числова відповідь: саме число, десятковий розділювач — крапка. Приклад: 25.5"
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

# Без цього app.logger.info(...) тоне — видно лише WARNING і гірше.
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
app.logger.setLevel(logging.INFO)

if not logging.getLogger().handlers or logging.getLogger().level > logging.INFO:
    app.logger.warning("logging INFO not enabled — model/answer lines will be hidden")


# ---------------------------------------------------------------------------
# Gemini REST
# ---------------------------------------------------------------------------
def call_gemini_once(model: str, image_bytes: bytes, mime: str = "image/jpeg"):
    """Повертає (status_code, parsed_or_raw)."""
    if not API_KEY:
        raise RuntimeError(
            "API-ключ не задано. Поклади його в api_key.txt поруч зі скриптом, "
            "~/.gemini_api_key або в змінну середовища GEMINI_API_KEY."
        )
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
        "generationConfig": {"temperature": 0, "maxOutputTokens": 320},
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


def normalize_answer(raw: str) -> str:
    """Зводить сиру відповідь моделі до одного з двох видів:
    цифрова відповідь (тільки цифри/пробіли/дефіси/крапка) або '!FAIL: ...'.
    Будь-що інше (порожньо, сміття, обірваний текст) теж стає '!FAIL'.
    Це головна страховка: навіть якщо модель проігнорує інструкцію і скаже
    'Недостатньо даних' / 'Ні' / 'The' / '0', ми не видаємо це за відповідь.
    """
    # Маркер '!' має бути ПЕРШИМ символом (можна з пробілами спереду),
    # інакше звичайна відповідь зі знаком оклику в кінці ставала б відмовою.
    t = (raw or "").strip()
    if t.startswith(ANSWER_FAIL):
        reason = t.lstrip("!").lstrip()
        for p in ("FAIL:", "FAIL", "НЕ РОЗПІЗНАНО:", "НЕ РОЗПІЗНАНО"):
            if reason.upper().startswith(p):
                reason = reason[len(p):].strip()
                break
        return "!FAIL: " + (reason or "не вдалося розпізнати завдання")
    up = t.upper()
    # Модель могла сказати 'НЕ РОЗПІЗНАНО: ...' замість '!FAIL: ...'
    if up.startswith("НЕ РОЗПІЗНАНО") or up.startswith("НЕ РОЗПІЗНАВ"):
        t = "!" + t
        up = t.upper()
    if up.startswith(ANSWER_FAIL):
        reason = t.lstrip("!").lstrip()
        for p in ("FAIL:", "FAIL", "НЕ РОЗПІЗНАНО:", "НЕ РОЗПІЗНАНО"):
            if reason.upper().startswith(p):
                reason = reason[len(p):].strip()
                break
        return "!FAIL: " + (reason or "не вдалося розпізнати завдання")
    # Слова-відмови → !!FAIL, навіть якщо всередині є цифри.
    low = t.lower().strip(".!? \n")
    # 'даних недостатньо' — той самий смисл, що 'недостатньо даних'
    if "недостатнь" in low or "недостають" in low:
        return "!FAIL: " + t
    if any(w in low for w in (
            "недостатньо", "не видно", "не вдалося", "немає", "не можу", "не можна",
            "розмито", "нечітко", "не чітко", "неможливо", "обрізан", "не стосується",
            "не тестове", "no data", "insufficient", "cannot", "blur",
            "неповний", "неповн", "скріншот", "скриншот", "уривок", "частково",
            "недостатня якість", "нечитабельн", "нечитабель")):
        return "!FAIL: " + t
    if low in ("ні", "no", "n/a", "null"):
        return "!FAIL: " + t
    # Однослівні заперечення ('Не', 'Немає даних') — це не цифра і не відповідь,
    # далі їх відсіє перевірка «лише цифри», але нехай причина буде людяною.
    if low in ("не", "nope"):
        return "!FAIL: модель не підтвердила прочитання"
    # Прибираємо можливі префікси типу 'ANSWER: 3' або 'Відповідь: 3'
    for p in (ANSWER_OK, "ВІДПОВІДЬ:", "ANSWER:"):
        if up.startswith(p):
            t = t[len(p):].strip()
            break
    # Сувора перевірка: лише цифри, пробіли, дефіси та крапка.
    # '2\n' допустиме, але 'The', "{'candidates'...", '2x' — ні.
    t = t.strip()
    if not t or not all(c in "0123456789 -." for c in t) or not any(c.isdigit() for c in t):
        return "!FAIL: модель не дала коректну відповідь"
    return t


def answer_key(text: str):
    """Канонічний ключ для порівняння відповідей між моделями.

    Моделі пишуть ту саму відповідь по-різному:
        "4 3 1 2"   "4-3-1-2"   "4 1 3 2"   "4, 3, 1, 2"   "4312"
    Для множинного вибору порядок номерів не має значення — це одна відповідь.
    Але для завдань на відповідність (пари «1-3») порядок важливий і самі
    пари впорядковані, тому:

      • є дефіси і довжини частин збігаються → це пари: ключ = кортеж пар,
        пари сортуються за першим елементом (порядок пар не важливий,
        порядок усередині пари — важливий);
      • інакше → це набір номерів/числа: ключ = відсортований кортеж токенів.

    Повертає tuple, придатний для порівняння; для '!FAIL' — ('fail', текст).
    """
    t = (text or "").strip()
    if t.startswith("!FAIL"):
        return ("fail", t)
    # токени: усе, що між розділювачами
    raw_parts = [x for x in t.replace(",", " ").replace(";", " ").split() if x]

    # чи це пари виду "1-3" / "1 - 3"?
    pairs = []
    all_pairs = True
    for part in raw_parts:
        if "-" not in part:
            all_pairs = False
            break
        left, _, right = part.partition("-")
        left, right = left.strip(), right.strip()
        if not (left.isdigit() and right.isdigit()) or not right:
            all_pairs = False
            break
        pairs.append((left, right))
    if all_pairs and pairs:
        return ("pairs", tuple(sorted(pairs, key=lambda p: (len(p[0]), p[0]))))

    # набір номерів: "4 3 1 2", "4-3-1-2" (дефіс як розділювач), "4312"
    tokens = [x for x in t.replace("-", " ").replace(",", " ").split() if x]
    if len(tokens) == 1 and tokens[0].isdigit() and len(tokens[0]) > 1 and "." not in tokens[0]:
        # "4312" -> цифри 4,3,1,2 (злиплі номери варіантів).
        # Свідомо НЕ робимо цього для чисел з однаковими цифрами ("22", "100")
        # і для довших за 4 символи — там уже схоже на числову відповідь.
        digits = list(tokens[0])
        if 2 <= len(digits) <= 4 and len(set(digits)) == len(digits):
            tokens = digits
    if not tokens:
        return ("empty",)
    return ("set", tuple(sorted(tokens, key=lambda x: (len(x), x))))


def canonical_answer(text: str) -> str:
    """Людський вигляд канонічного ключа — для логів і повідомлень."""
    k = answer_key(text)
    if k[0] == "fail":
        return text
    if k[0] == "pairs":
        return " ".join(f"{a}-{b}" for a, b in k[1])
    if k[0] == "set":
        return " ".join(k[1])
    return text


def image_sharpness(image_bytes: bytes) -> float:
    """Оцінка різкості: дисперсія лапласіана (чим більше — тим різкіше).

    Потрібна, щоб із кількох кадрів ручки вибрати найменш змазаний.
    Без Pillow або на битому файлі повертає 0.0 — тоді вибір іде за розміром.
    """
    if not HAVE_PIL:
        return 0.0
    try:
        import io
        import statistics
        from PIL import Image, ImageFilter
        with Image.open(io.BytesIO(image_bytes)) as im:
            g = im.convert("L")
            # зменшуємо великі кадри: оцінка не залежить від роздільності
            if max(g.size) > 640:
                k = 640 / max(g.size)
                g = g.resize((max(1, int(g.width * k)), max(1, int(g.height * k))))
            lap = g.filter(ImageFilter.Kernel((3, 3), (0, 1, 0, 1, -4, 1, 0, 1, 0), scale=1, offset=128))
            hist = lap.histogram()
        total = sum(hist)
        if not total:
            return 0.0
        mean = sum(i * c for i, c in enumerate(hist)) / total
        var = sum((i - mean) ** 2 * c for i, c in enumerate(hist)) / total
        return var
    except Exception:
        return 0.0


def pick_best_frame(frames):
    """Повертає (найкращий кадр, список оцінок, індекс)."""
    scores = [image_sharpness(f) for f in frames]
    best = 0
    for i, sc in enumerate(scores):
        if sc > scores[best]:
            best = i
    return frames[best], scores, best


def call_gemini(image_bytes: bytes, mime: str = "image/jpeg"):
    """Пробує моделі по колу і повертає відповідь ЛИШЕ за згодою (кворумом).

    Правило безпеки: цифри повертаємо тільки якщо щонайменше ДВІ різні моделі
    незалежно дійшли тієї самої відповіді. Якщо моделі розходяться
    (2 проти 3 — на реальних фото це трапляється) або відповіла лише одна,
    повертаємо '!FAIL: ...'. Ніколи не віддаємо першу відповідь, що прийшла,
    і ніколи не вибираємо 'більшість голосів' як заспокоєння.
    """
    order = MODEL_CANDIDATES[_current_model["n"]:] + MODEL_CANDIDATES[:_current_model["n"]]
    votes = {}    # канонічний ключ -> {"text": людський вигляд, "models": [...]}
    fails = []    # '!FAIL: причина'
    errors = []   # 429 / 404 / таймаут
    for model in order:
        code, data = call_gemini_once(model, image_bytes, mime)
        if code != 200:
            msg = str(data)[:200]
            errors.append("429 (квота)" if "429" in msg else f"{model}: {code}")
            app.logger.warning("model=%s failed: %s", model, msg)
            continue
        try:
            out = data["candidates"][0]["content"]["parts"][0]["text"].strip()
        except Exception:
            out = ""
        out = normalize_answer(out)
        app.logger.warning("model=%s 200 -> %s", model, out)
        if out.startswith("!FAIL"):
            fails.append(out)
        elif out == "0":
            # '0' — не номер варіанта (нумерація з 1), а ознака невпевненості
            fails.append("!FAIL: модель не впевнена у відповіді")
        else:
            key = answer_key(out)
            slot = votes.setdefault(key, {"text": canonical_answer(out), "variants": [], "models": []})
            slot["models"].append(model)
            if out not in slot["variants"]:
                slot["variants"].append(out)

    if not votes:
        for f in fails:
            return f
        return "!FAIL: моделі недоступні (" + ", ".join(dict.fromkeys(errors[:3])) + ")"

    detail = "; ".join(f"{v['text']}: {len(v['models'])}x" for v in votes.values())
    if len(votes) == 1:
        slot = next(iter(votes.values()))
        answer, ms = slot["text"], slot["models"]
        if len(slot["variants"]) > 1:
            app.logger.info("same answer written differently: %s", slot["variants"])
        if len(ms) >= 2:
            _current_model["n"] = MODEL_CANDIDATES.index(ms[0])
            app.logger.info("answer=%s agreed_by=%s", answer, ms)
            return answer
        app.logger.warning("no quorum: only %s said %s (fails=%d errors=%d)",
                           ms, answer, len(fails), len(errors))
        return "!FAIL: немає підтвердження відповіді (" + detail + ")"

    app.logger.warning("models disagree: %s", detail)
    return "!FAIL: моделі розходяться у відповіді (" + detail + ")"


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
    """Приймає ОДИН JPEG (старий протокол) або КІЛЬКА кадрів у multipart.

    Ручка надсилає multipart/form-data з полями image0, image1, ... — сервер
    оцінює різкість кожного й бере найкращий, решту зберігає як *_alt.jpg.
    """
    frames = []

    # Тіло читаємо ПЕРШИМ і з кешем: після звернення до request.files
    # сирий потік уже спожитий, і get_data() повертає порожньо.
    raw_body = request.get_data(cache=True)

    if request.files:
        # multipart: поля image0..imageN
        for key in sorted(request.files.keys(), key=lambda k: (len(k), k)):
            f = request.files[key]
            data = f.read()
            if data:
                frames.append((f.filename or key, data))

    if not frames and raw_body:
        # старий протокол: сирий JPEG у тілі
        frames.append((None, raw_body))

    if not frames:
        app.logger.warning("upload: no frames (files=%s body=%d bytes)",
                           list(request.files.keys()), len(raw_body))
        return "Empty body", 400

    saved = []
    for name, data in frames:
        if not data:
            continue
        if not (len(data) >= 2 and data[0] == 0xFF and data[1] == 0xD8):
            app.logger.warning("frame does not start with JPEG SOI (0xFFD8)")
            return "Not a JPEG", 400
        path = os.path.join(UPLOAD_DIR, make_filename())
        try:
            with open(path, "wb") as fh:
                fh.write(data)
        except Exception as e:
            app.logger.error("write failed: %s", e)
            return "Write failed", 500
        if HAVE_PIL:
            try:
                from PIL import Image
                with Image.open(path) as im:
                    im.verify()
            except Exception as e:
                app.logger.error("invalid image %s: %s", path, e)
                return "Invalid image", 400
        saved.append((path, data))

    best_data = saved[0][1] if len(saved) == 1 else None
    if len(saved) > 1:
        best_data, scores, best_idx = pick_best_frame([d for _, d in saved])
        app.logger.info("frames sharpness: %s -> pick #%d",
                        ["%.1f" % sc for sc in scores], best_idx + 1)
        # перейменовуємо, щоб зрозуміти у дампах, який кадр обрано
        for i, (path, _) in enumerate(saved):
            if i == best_idx:
                continue
            base, ext = os.path.splitext(path)
            try:
                os.rename(path, base + "_alt" + ext)
            except Exception:
                pass

    t0 = time.time()
    answer = call_gemini(best_data)
    app.logger.info("upload frames=%d in %.1fs -> %s",
                    len(saved), time.time() - t0, answer[:80])
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
    if API_KEY:
        print(f"[i] ключ: {KEY_SOURCE} ({API_KEY[:6]}...{API_KEY[-4:]})")
    else:
        print("[!] API-ключ не задано. Поклади його у api_key.txt поруч зі скриптом,")
        print("    у ~/.gemini_api_key або в змінну GEMINI_API_KEY.")
    print("[i] слухаю 0.0.0.0:5000")
    # Для локальної мережі з ручки: не включати reloader/debug
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
