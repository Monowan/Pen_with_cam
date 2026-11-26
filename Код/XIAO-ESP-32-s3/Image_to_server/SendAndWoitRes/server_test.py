# server_test.py — заглушка замість Gemini.
#
# Приймає ті самі запити, що й server.py (POST /upload з JPEG у тілі), але
# нікуди не ходить: повертає ту відповідь, яку ти ввів у полі.
#
# Запуск:
#   python3 server_test.py            # порт 5000
#   python3 server_test.py 5050       # інший порт
#
# Відповідь задається прямо в браузері: відкрий http://<ip-мака>:<порт>/,
# впиши текст у поле — він застосовується одразу, без кнопок і збережень.
import os
import sys
import time

from flask import Flask, request, Response

HERE = os.path.dirname(os.path.abspath(__file__))
UPLOAD_DIR = os.path.join(HERE, "uploads_test")
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = Flask(__name__)

_state = {"text": "3"}
LOG = []

PAGE = """<!doctype html><html lang="uk"><meta charset="utf-8">
<title>Test server — заглушка Gemini</title>
<style>
 body{font:14px -apple-system,Segoe UI,Roboto,sans-serif;max-width:820px;margin:24px auto;padding:0 16px;color:#111}
 h1{font-size:18px;margin:0 0 4px}
 .muted{color:#666;font-size:12px}
 textarea{width:100%;height:110px;font:13px ui-monospace,Menlo,monospace;padding:10px;box-sizing:border-box;
          border:1px solid #ccc;border-radius:8px}
 textarea:focus{outline:2px solid #4a90d9;border-color:#4a90d9}
 .status{font-size:12px;color:#0a7d32;margin:6px 0 0;height:16px}
 .box{border:1px solid #ddd;border-radius:8px;padding:12px;margin:16px 0}
 pre{background:#f6f6f6;padding:8px;border-radius:6px;overflow:auto;margin:8px 0 0;font-size:12px}
 code{background:#f0f0f0;padding:1px 4px;border-radius:3px;font-size:12px}
</style>
<h1>Test server — заглушка Gemini</h1>
<div class="muted">Пиши відповідь у поле — вона застосовується одразу, без кнопок.</div>

<div class="box">
  <form id="f">
    <textarea id="t" autofocus placeholder="3">__TEXT__</textarea>
  </form>
  <div class="status" id="s"></div>
  <div class="muted" style="margin-top:6px">Приклади:
    <code>3</code> · <code>1 4 5</code> ·
    <code>LED:R:ON:300;LED:G:ON:300;LED:B:SP:100:200:3</code>
  </div>
</div>

<div class="box">
  <b>Журнал запитів</b>
  <pre id="log">__LOG__</pre>
</div>

<script>
const t = document.getElementById('t');
const s = document.getElementById('s');
let timer = null;

async function save() {
  const r = await fetch('/set?text=' + encodeURIComponent(t.value), {method:'POST'});
  s.textContent = r.ok ? 'збережено ' + new Date().toLocaleTimeString() : 'помилка';
}

t.addEventListener('input', () => {
  clearTimeout(timer);
  s.textContent = '...';
  timer = setTimeout(save, 250);   // авто-збереження через 0.25 с після паузи
});

t.addEventListener('keydown', e => {
  if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) { clearTimeout(timer); save(); }
});

async function refreshLog() {
  try {
    const r = await fetch('/log');
    document.getElementById('log').textContent = await r.text();
  } catch(e) {}
}
setInterval(refreshLog, 2000);
</script>
</html>"""


def render():
    return (PAGE
            .replace("__TEXT__", _state["text"])
            .replace("__LOG__", "\n".join(LOG[-12:])))


@app.route("/", methods=["GET"])
def index():
    return Response(render(), mimetype="text/html; charset=utf-8")


@app.route("/set", methods=["GET", "POST"])
def set_text():
    t = request.values.get("text")
    if t is None:
        return "missing ?text=", 400
    _state["text"] = t
    app.logger.info("answer = %r", t)
    return "ok", 200


@app.route("/log", methods=["GET"])
def log():
    return Response("\n".join(LOG[-12:]) or "(ще немає)", mimetype="text/plain")


@app.route("/health", methods=["GET"])
def health():
    return {"status": "test-stub", "answer": _state["text"]}


@app.route("/upload", methods=["POST"])
def upload():
    data = request.get_data()
    name = f"photo_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
    if data:
        with open(os.path.join(UPLOAD_DIR, name), "wb") as f:
            f.write(data)
    answer = _state["text"]
    LOG.append(f"{time.strftime('%H:%M:%S')} {len(data):>7}B -> {answer[:60]!r}")
    app.logger.info("upload %d bytes -> %r", len(data), answer)
    return answer, 200


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
    print(f"[i] uploads у {UPLOAD_DIR}")
    print(f"[i] Web UI: http://127.0.0.1:{port}/")
    app.run(host="0.0.0.0", port=port, debug=False, threaded=True)
