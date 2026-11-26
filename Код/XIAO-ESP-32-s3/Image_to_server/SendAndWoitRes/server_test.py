# server_test.py — заглушка замість Gemini.
#
# Приймає ті самі запити, що й server.py (POST /upload з JPEG у тілі), але
# нікуди не ходить: повертає ту відповідь, яку ти задав.
#
# Запуск:
#   python3 server_test.py
# Порт за замовчуванням 5000 (як у server.py), Wi-Fi ручки налаштовувати не треба.
#
# --- Як задати відповідь ---
# За замовчуванням відповідь = "3" (звичайна відповідь тесту).
#
# Варіант 1 — Web UI, найзручніше:
#   відкрий http://<ip-твого-мака>:5000/ у браузері,
#   впиши відповідь у поле (можна з переносами рядків) і натисни «Зберегти».
#
# Вариант 2 — через URL:
#   http://<ip>:5000/set?text=LED:R:ON:300;LED:G:ON:300;LED:B:ON:300
#
# Варіант 3 — чергою: кожен наступний /upload з'єдає наступний рядок файлу
#   queue.txt (один рядок = одна відповідь). Коли файл закінчився —
#   відповідає останнім рядком.
#
#   /queue?add=3    — додати відповідь у кінець черги
#   /queue?clear=1  — очистити чергу
#   GET /queue      — показати чергу
#
# Адреса в скетчі: serverHost = IP мака, serverPort = 5000, serverPath = "/upload".
import os
import time
from flask import Flask, request, Response

HERE = os.path.dirname(os.path.abspath(__file__))
UPLOAD_DIR = os.path.join(HERE, "uploads_test")
QUEUE_FILE = os.path.join(HERE, "queue.txt")
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = Flask(__name__)

# одне змінне значення "остання задана відповідь"
_state = {"text": "3"}


def _queue_read():
    if not os.path.exists(QUEUE_FILE):
        return []
    with open(QUEUE_FILE, encoding="utf-8") as f:
        return [ln.rstrip("\n") for ln in f if ln.strip()]


def _queue_write(items):
    with open(QUEUE_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(items) + ("\n" if items else ""))


def _next_answer():
    """Якщо є черга — бере наступний рядок і з'їдає його, інакше — _state['text']."""
    items = _queue_read()
    if items:
        ans = items.pop(0)
        _queue_write(items)
        app.logger.info("queue -> %r (left %d)", ans, len(items))
        return ans
    return _state["text"]


PAGE = """<!doctype html><html lang="uk"><meta charset="utf-8">
<title>Test server — заглушка Gemini</title>
<style>
 body{font:14px -apple-system,Segoe UI,Roboto,sans-serif;max-width:760px;margin:24px auto;padding:0 16px;color:#111}
 h1{font-size:18px;margin:0 0 4px} .muted{color:#666;font-size:12px;margin-bottom:16px}
 textarea{width:100%;height:90px;font:13px ui-monospace,Menlo,monospace;padding:8px;box-sizing:border-box}
 button{padding:8px 16px;margin:8px 8px 0 0;font-size:14px;cursor:pointer}
 .box{border:1px solid #ddd;border-radius:8px;padding:12px;margin-bottom:16px}
 pre{background:#f6f6f6;padding:8px;border-radius:6px;overflow:auto;margin:8px 0 0}
 code{background:#f0f0f0;padding:1px 4px;border-radius:3px}
</style>
<h1>Test server — заглушка Gemini</h1>
<div class="muted">Сервер відповідає тим, що тут записано, і нікуди не ходить.</div>

<div class="box">
  <b>Відповідь на кожен /upload</b>
  <form method="post" action="/set">
    <textarea name="text" placeholder="3">__TEXT__</textarea>
    <button type="submit">Зберегти</button>
  </form>
  <div class="muted">Приклади:
    <code>3</code> ·
    <code>1 4 5</code> ·
    <code>LED:R:ON:300;LED:G:ON:300;LED:B:SP:100:200:3</code>
  </div>
</div>

<div class="box">
  <b>Черга відповідей</b> <span class="muted">(кожен наступний /upload бере наступний рядок)</span>
  <form method="post" action="/queue">
    <textarea name="add" placeholder="один рядок = одна відповідь"></textarea>
    <button type="submit">Додати в чергу</button>
    <button type="submit" name="clear" value="1">Очистити</button>
  </form>
  <pre>__QUEUE__</pre>
</div>

<div class="box">
  <b>Журнал останніх запитів</b>
  <pre>__LOG__</pre>
</div>
</html>"""

LOG = []


@app.route("/", methods=["GET"])
def index():
    html = (PAGE
            .replace("__TEXT__", _state["text"])
            .replace("__QUEUE__", "\n".join(_queue_read()) or "(порожня)")
            .replace("__LOG__", "\n".join(LOG[-15:]) or "(ще немає)"))
    return Response(html, mimetype="text/html; charset=utf-8")


@app.route("/set", methods=["GET", "POST"])
def set_text():
    t = request.values.get("text")
    if t is None:
        return "missing ?text=", 400
    _state["text"] = t
    app.logger.info("answer set to %r", t)
    if request.method == "POST":
        return Response(PAGE.replace("__TEXT__", t)
                        .replace("__QUEUE__", "\n".join(_queue_read()) or "(порожня)")
                        .replace("__LOG__", "\n".join(LOG[-15:]) or "(ще немає)"),
                        mimetype="text/html; charset=utf-8")
    return f"ok: {t}", 200


@app.route("/queue", methods=["GET", "POST"])
def queue():
    if request.method == "POST" and request.values.get("clear"):
        _queue_write([])
        return Response("cleared", mimetype="text/plain")
    if request.method == "POST":
        add = request.values.get("add", "")
        if add.strip():
            items = _queue_read() + [ln for ln in add.splitlines() if ln.strip()]
            _queue_write(items)
        return Response("ok", mimetype="text/plain")
    if request.args.get("add"):
        items = _queue_read() + [request.args["add"]]
        _queue_write(items)
    if request.args.get("clear"):
        _queue_write([])
    return Response("\n".join(_queue_read()) or "(порожня)", mimetype="text/plain")


@app.route("/upload", methods=["POST"])
def upload():
    data = request.get_data()
    name = f"photo_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
    if data:
        with open(os.path.join(UPLOAD_DIR, name), "wb") as f:
            f.write(data)
    answer = _next_answer()
    LOG.append(f"{time.strftime('%H:%M:%S')} {name} {len(data)}B -> {answer[:60]!r}")
    app.logger.info("upload %s %d bytes -> %r", name, len(data), answer)
    return answer, 200


@app.route("/health", methods=["GET"])
def health():
    return {"status": "test-stub", "answer": _state["text"], "queue": len(_queue_read())}


if __name__ == "__main__":
    print(f"[i] uploads у {UPLOAD_DIR}")
    print("[i] Web UI: http://127.0.0.1:5000/")
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
