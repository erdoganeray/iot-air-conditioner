// Pil ve adaptör izleme, kritik pilde derin uyku. Ayrıntı: docs/MIMARI.md §10
#pragma once

#include <Arduino.h>

namespace hub::power {

// setup()'ın en başında: kritik pil uykusundan uyanıldıysa ve durum düzelmediyse
// hiçbir şey başlatmadan tekrar uyur.
void earlyCheck();
void begin();
void loop();

}  // namespace hub::power
