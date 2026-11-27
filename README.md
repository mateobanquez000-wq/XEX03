# XEX03 - afinador vocal (Fase 2)

Plugin VST3 para FL Studio. Motor propio en C++ (`Source/dsp/XexEngine.h`), sin dependencias de JUCE,
probado con senales sinteticas (`tests/test_engine.cpp`, 21 pruebas).

## Controles
- Key / Scale: eligen las notas permitidas (se reflejan en el teclado inferior, que tambien se edita a mano).
- Retune Speed: 0 ms = correccion instantanea (efecto robotico); valores altos = glide natural.
- Flex-Tune: tolerancia alrededor de la nota; las desviaciones pequenas se corrigen menos.
- Humanize: correccion mas lenta en notas sostenidas.
- Natural Vibrato: 0 % conserva el vibrato original, -100 % lo elimina, +100 % lo duplica.
- Mix: mezcla entre senal original y procesada.
- Transpose (-12..+12 st) y Conform to Scale (la transposicion cae en notas de la escala).
- Detune (+-100 cents): desplaza la referencia de afinacion (A4 = 440 Hz).
- Formant (+-12 st): desplaza el timbre sin cambiar la nota. Formant Preserve apagado = las formantes siguen a la altura.
- Vibrato creativo: Shape (Off/Sine/Sawtooth/Square), Rate, Delay, Onset, Variation, Depth (cents) y Tremolo.
- Latencia: Normal (voces desde 70 Hz, ~28 ms), Bajo (110 Hz, ~18 ms), Ultra (180 Hz, ~11 ms a 48 kHz).
- Visualizador: ultimos ~6 s. Rojo = entrada, azul = nota objetivo, verde = salida.

## Estructura
CMakeLists.txt, Source/ (plugin + dsp/), tests/, .github/workflows/build.yml (compila en GitHub).

## Probar el motor en cualquier computador
g++ -O2 -std=c++17 tests/test_engine.cpp -o test_engine && ./test_engine
