import random
import time
import numpy as np
import simpleaudio as sa

# Азбука Морзе для цифр
morse_digits = {
    "0": "-----",
    "1": ".----",
    "2": "..---",
    "3": "...--",
    "4": "....-",
    "5": ".....",
    "6": "-....",
    "7": "--...",
    "8": "---..",
    "9": "----."
}

def generate_tone(frequency, duration_ms):
    sample_rate = 44100
    t = np.linspace(0, duration_ms / 1000, int(sample_rate * duration_ms / 1000), False)
    tone = np.sin(frequency * t * 2 * np.pi) * 0.5
    audio = (tone * 32767).astype(np.int16)  # 16-бітний PCM
    return audio

def play_beep(frequency, duration_ms):
    audio = generate_tone(frequency, duration_ms)
    play_obj = sa.play_buffer(audio, 1, 2, 44100)
    play_obj.wait_done()

def play_morse(code):
    freq = 800  # частота сигналу (Гц)
    dot = 75   # тривалість крапки (мс)
    dash = dot * 3

    for symbol in code:
        if symbol == ".":
            play_beep(freq, dot)
        elif symbol == "-":
            play_beep(freq, dash)
        time.sleep(dot / 1000)  # пауза між елементами
    time.sleep(dot * 3 / 1000)  # пауза між літерами

def main():
    while True:  # нескінченний цикл
        number = str(random.randint(0, 9))  # випадкове число 0–9
        morse = morse_digits[number]

        print("\nВідтворення коду Морзе цифри...")
        play_morse(morse)

        guess = input("Яка це цифра? (або 'q' щоб вийти) > ")
        if guess.lower() == "q":
            print("Вихід із гри. 👋")
            break
        elif guess == number:
            print("✅ Правильно!")
        else:
            print(f"❌ Неправильно! Це була цифра {number}.")

if __name__ == "__main__":
    main()
