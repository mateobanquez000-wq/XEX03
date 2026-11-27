# XEX03 - afinador vocal (Fase 1)

Plugin VST3 para FL Studio. Motor propio en C++ (`Source/dsp/XexEngine.h`), sin dependencias de JUCE,
probado con senales sinteticas (`tests/test_engine.cpp`).

## Controles
- Key / Scale: eligen las notas permitidas (se reflejan en el teclado inferior, que tambien se edita a mano).
- Retune Speed: 0 ms = correccion instantanea (efecto robotico); valores altos = glide natural.
- Flex-Tune: tolerancia alrededor de la nota; las desviaciones pequenas se corrigen menos.
- Humanize: correccion mas lenta en notas sostenidas.
- Natural Vibrato: 0 % conserva el vibrato original, -100 % lo elimina, +100 % lo duplica.
- Mix: mezcla entre senal original y procesada.
- Latencia: Normal (voces desde 70 Hz, ~28 ms), Bajo (110 Hz, ~18 ms), Ultra (180 Hz, ~11 ms a 48 kHz).

## Estructura
CMakeLists.txt, Source/ (plugin + dsp/), tests/, .github/workflows/build.yml (compila en GitHub).

## Probar el motor en cualquier computador
g++ -O2 -std=c++17 tests/test_engine.cpp -o test_engine && ./test_engine
