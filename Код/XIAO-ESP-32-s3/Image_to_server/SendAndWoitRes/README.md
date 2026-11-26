# Ruchka → servер → Gemini

## Стан (перевірено 2026-11-26)
- Flask-сервер працює, приймає JPEG, пише в `uploads/`.
- **Gemini не відповідає**: ключ `AIzaSyBL...fAQ` живий, але Google віддає
  `403 PERMISSION_DENIED` / `ACCESS_DENIED` на *всі* методи API
  (навіть на `GET /v1beta/models`). Це блокування проєкту в Google Cloud,
  а не проблема моделі чи коду.

## Що зробити (ти, 2 хвилини)
1. https://aistudio.google.com/apikey → акаунт з тим ключем у проєкті.
2. Там буде підказка «API not enabled / project disabled». Увімкнути
   **Generative Language API** або створити новий ключ у **новому проєкті**
   (старий проєкт або видалено, або заблоковано за порушення).
3. Скопіювати новий ключ у `api_key.txt` (один рядок). Нічого в коді міняти не треба.
4. Перевірити: `./.venv/bin/python check_key.py`

## Запуск сервера
```bash
cd ~/Desktop/нмт/Код/XIAO-ESP-32-s3/Image_to_server/SendAndWoitRes
./.venv/bin/python server.py
```
Порт 5000, `0.0.0.0` — доступний по LAN для ручки.
- `GET /health` — стан, поточна модель, чи є Pillow
- `POST /upload` — JPEG у тілі, відповідь = відповідь Gemini
- `POST /test?file=/path/to.jpg` — прогнати локальний файл

## Моделі
`server.py` перебирає по-колу: gemini-2.5-flash → 2.0-flash → flash-latest → 2.5-flash-lite.
Тому при зміні назви моделі Google сервер не падає.

## Додатково виправлено в скетчі SentAndWoitRes.ino
- Додано `#include <WiFiClient.h>`.
- Додано виклик `esp_sleep_enable_hold()` / коректна послідовність із `Serial.flush()`
  перед deep sleep, щоб не втрачати рядки логу.
- Камера: `config.fb_count = 2` лише при UXGA; при `jpeg_quality = 5` кадр великий
  (~300+ КБ), це очікувано.
