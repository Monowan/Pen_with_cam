# server_test.py — заглушка замість Gemini з керуванням прямо з консолі.
#
# Приймає ті самі запити, що й server.py (POST /upload з JPEG у тілі), але
# нікуди не ходить: повертає рядок, який ти написав у консолі.
#
# Запуск:
#   python3 server_test.py            # порт 5000
#   python3 server_test.py 5050       # інший порт
#
# Керування в консолі (у будь-який момент, поки сервер працює):
#   просто текст + Enter       -> новий рядок відповіді (застосовується одразу)
#   /show                      -> показати поточну відповідь
#   /log                       -> останні запити від ручки
#   /status                    -> все вкупі
#   /quit                      -> зупинити сервер
#
# Порожній рядок — ігнорується. Пробіли зберігаються як є (не обрізаються).
# Сервер слухає запити в тому ж процесі, тож усе працює без переходів у браузер.
import os
import sys
import threading
import time

from flask import Flask, request

HERE = os.path.dirname(os.path.abspath(__file__))
UPLOAD_DIR = os.path.join(HERE, "uploads_test")
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = Flask("test_stub")
app.logger.setLevel(40)          # глушимо werkzeug-спам у консолі

_state = {"text": "3"}
LOG = []
_running = threading.Event()
_running.set()


@app.route("/upload", methods=["POST"])
def upload():
    data = request.get_data()
    name = f"photo_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
    if data:
        with open(os.path.join(UPLOAD_DIR, name), "wb") as f:
            f.write(data)
    answer = _state["text"]
    LOG.append(f"{time.strftime('%H:%M:%S')} {len(data):>7}B -> {answer[:60]!r}")
    print(f"  <- {len(data):>7} B  |  {answer[:70]}", flush=True)
    return answer, 200


@app.route("/health", methods=["GET"])
def health():
    return {"status": "test-stub", "answer": _state["text"]}


def console_loop():
    """Основний потік: читає те, що вводиш у консолі."""
    print()
    print("  Введи текст відповіді і натисни Enter — він застосовується одразу.")
    print("  /show  /log  /status  /quit")
    print(f"  поточна відповідь: {_state['text']!r}")
    print()

    for line in sys.stdin:
        cmd = line.rstrip("\n")
        if cmd == "/quit":
            print("  зупиняю сервер...")
            _running.clear()
            os._exit(0)

        if cmd == "":
            continue

        if cmd == "/show":
            print(f"  відповідь: {_state['text']!r}")
        elif cmd == "/log":
            print("  " + ("\n  ".join(LOG[-10:]) if LOG else "(ще немає запитів)"))
        elif cmd == "/status":
            print(f"  відповідь: {_state['text']!r}")
            print(f"  запитів:   {len(LOG)}")
            if LOG:
                print("  останній:  " + LOG[-1])
        else:
            _state["text"] = cmd
            print(f"  OK -> {cmd!r}")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5000

    def serve():
        app.run(host="0.0.0.0", port=port, debug=False, threaded=True, use_reloader=False)

    threading.Thread(target=serve, daemon=True).start()
    time.sleep(1)
    print(f"Test server на 0.0.0.0:{port}  (POST /upload)")
    try:
        console_loop()
    except KeyboardInterrupt:
        print("\n  зупинено")
