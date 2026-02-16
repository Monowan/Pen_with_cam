void setup() {
  Serial.begin(115200);
  delay(1000); // Даємо час послідовному порту завантажитись

  Serial.println("\n--- Тестування PSRAM на ESP32-CAM ---");

  // 1. Перевірка чи PSRAM взагалі активована в налаштуваннях
  if (!psramInit()) {
    Serial.println("ПОМИЛКА: PSRAM не ініціалізовано! Перевірте налаштування Board -> PSRAM: Enabled");
    return;
  }

  // 2. Вивід загального обсягу пам'яті
  size_t total_psram = ESP.getPsramSize();
  size_t free_psram = ESP.getFreePsram();
  
  Serial.printf("Загальний обсяг PSRAM: %d байт (%.2f MB)\n", total_psram, (float)total_psram / (1024 * 1024));
  Serial.printf("Вільний обсяг PSRAM: %d байт\n", free_psram);

  // 3. Тест запису та зчитування
  Serial.println("Запуск тесту запису/зчитування...");

  const int testSize = 10000; // Розмір масиву для тесту
  int* testArray = (int*)ps_malloc(testSize * sizeof(int)); // Виділяємо пам'ять саме в PSRAM

  if (testArray == NULL) {
    Serial.println("ПОМИЛКА: Не вдалося виділити пам'ять у PSRAM");
    return;
  }

  // Записуємо дані
  for (int i = 0; i < testSize; i++) {
    testArray[i] = i * 2;
  }

  // Перевіряємо дані
  bool success = true;
  for (int i = 0; i < testSize; i++) {
    if (testArray[i] != i * 2) {
      Serial.printf("ПОМИЛКА в комірці %d! Очікували %d, отримали %d\n", i, i * 2, testArray[i]);
      success = false;
      break;
    }
  }

  if (success) {
    Serial.println("РЕЗУЛЬТАТ: Тест PSRAM пройдено успішно!");
  } else {
    Serial.println("РЕЗУЛЬТАТ: Тест провалено. Можливі апаратні проблеми.");
  }

  free(testArray); // Звільняємо пам'ять
}

void loop() {
  // Нічого не робимо
}
