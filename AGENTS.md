# Reglas del Proyecto (Kingdom Hearts Chain of Memories Recomp)

El proyecto debe compilar y funcionar en **Linux** (compilación en contenedor Docker) y en **Windows** (MSVC). Las dos plataformas usan directorios de compilación distintos para no pisar la caché de CMake: `build/` para Linux/Docker y `build-vs/` para Windows.

## 1. Actualización Obligatoria del Binario (`KHCOMRecomp`)

Siempre que se realice cualquier modificación, corrección o agregado en el código fuente (C/C++ en `src/`, `generated/`, encabezados, `CMakeLists.txt`, etc.), o en el submódulo `external/gba-improvements`:

### Imagen de compilación (una sola vez)

La imagen `khcom-builder` se construye con el `Dockerfile` del repositorio:
```bash
docker build -t khcom-builder docker/
```

En Git Bash de Windows conviene exportar `MSYS_NO_PATHCONV=1` antes de usar `docker run` con rutas montadas. En los ejemplos, `<repo>` es la ruta raíz del repositorio (por ejemplo `C:\Users\RafaelInostroza\Desktop\KHR`).

### Linux (contenedor Docker)

1. **Recompilar:**
   ```bash
   docker run --rm -v "<repo>:/src" -w /src khcom-builder bash -c "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release"
   ```
2. **Binario de la raíz:** el paso posterior a la compilación copia automáticamente `build/KHCOMRecomp` a `./KHCOMRecomp`. Verificar que quedó igual (`cmp build/KHCOMRecomp ./KHCOMRecomp`).
3. **Tests:**
   ```bash
   docker run --rm -v "<repo>:/src" -w /src khcom-builder bash -c "./build/khcom_unit_tests; python3 tests/test_runtime_functions.py"
   ```
   `tests/test_runtime_functions.py` reescribe `keybinds.ini` (fin de línea); después ejecutar `git checkout -- keybinds.ini`.

### Windows (MSVC)

1. **Recompilar:** desde PowerShell, en la raíz del repositorio:
   ```powershell
   .\build.ps1 -BuildDir build-vs
   ```
   Genera `KHCOMRecomp.exe` y `SDL2.dll` en la raíz del repositorio (ambos ignorados por git). Para compilar también el ejecutable de tests: `.\build.ps1 -BuildDir build-vs -Target khcom_unit_tests`.
2. **Tests:** desde la raíz del repositorio, `.\build-vs\Release\khcom_unit_tests.exe` (con `SDL2.dll` accesible, que la compilación ya copia junto al ejecutable).
3. **Arranque de prueba sin pantalla (opcional):** con `SDL_VIDEODRIVER=dummy` y `SDL_AUDIODRIVER=dummy`, pasando las rutas entre comillas: `.\KHCOMRecomp.exe --rom "roms\<archivo>.gba" --bios "bios\gba (W).gba"`. Con `KHCOM_LOG_INTERPOSE=1` imprime el estado de las intercepciones de SDL.

### Qué plataformas verificar

- Cambios en C++ del juego, `CMakeLists.txt`, intercepciones SDL o en el submódulo: compilar y probar **ambas** plataformas.
- Cambios solo de documentación o de datos que no se compilan: no hace falta recompilar.

### Regla de finalización

Nunca responder al usuario dando por terminada una tarea de código sin haber compilado, actualizado el binario de la raíz (`./KHCOMRecomp` y `KHCOMRecomp.exe` cuando se compila en Windows) y ejecutado los tests de las plataformas afectadas. Si una plataforma no se pudo compilar o probar, decirlo explícitamente.

## 2. Biblioteca `gba-improvements` (submódulo)

- Vive en `external/gba-improvements` como submódulo git y contiene las mejoras reutilizables (filtros, OSD, controles, audio, interceptación de SDL). No debe contener referencias a este juego.
- Los cambios de la biblioteca se hacen en su repositorio: se prueban, se hace commit y push allí, y después se actualiza el puntero del submódulo en este repositorio. Un commit de este repositorio con el puntero viejo, o sin el puntero actualizado, rompe la compilación de un checkout limpio.
- Todo lo específico del juego (zonas de HUD, hooks, widescreen, diálogo, música) vive en `src/`; la biblioteca se configura desde `src/gbaimp_integration.cpp`.
- Ver `docs/GBA_IMPROVEMENTS_INTEGRATION.md`.

## 3. Documentación

- Mapa del código y flujos de datos: `docs/ARCHITECTURE.md`. Integración de la biblioteca: `docs/GBA_IMPROVEMENTS_INTEGRATION.md`. Overlay de texto HD: `docs/TEXT_OVERLAY_ENGINE.md` y `docs/TEXT_OVERLAY_RENDER.md`. Captura sin pantalla: `docs/CAPTURE_HARNESS.md`.
- Al cambiar ajustes (claves de `khcom_config.ini`, menú), variables de entorno `KHCOM_*`, herramientas de `tools/` o el flujo de compilación, actualizar `README.md` y el documento correspondiente en el mismo cambio. No documentar como activas funciones eliminadas (el antiguo texto mejorado dentro del motor; la opción `hud_anchoring`).
- La biblioteca `external/gba-improvements` debe mantenerse agnóstica del juego: su documentación (en inglés) no menciona ningún juego concreto.
- El CI (`.github/workflows/ci.yml`) compila ambas plataformas y ejecuta solo `tests/test_runtime_functions.py`; `khcom_unit_tests` se ejecuta localmente según la sección 1.
