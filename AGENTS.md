# Reglas del Proyecto (Kingdom Hearts Chain of Memories Recomp)

## 1. Actualización Obligatoria del Binario (`KHCOMRecomp`)

Siempre que se realice cualquier modificación, corrección o agregado en el código fuente (C/C++ en `src/`, `generated/`, encabezados, etc.):

1. **Recompilar inmediatamente el proyecto:**
   Compilar el ejecutable dentro del contenedor de compilación Podman:
   ```bash
   podman run --rm -v /home/rafael/KingdomHeartsChainOfMemoriesRecomp:/src:Z -w /src localhost/khcom-builder cmake --build build --config Release
   ```

2. **Sincronizar el binario de la raíz:**
   Copiar siempre el ejecutable resultante a la raíz del repositorio para que la versión que ejecuta el usuario esté 100% actualizada:
   ```bash
   cp build/KHCOMRecomp ./KHCOMRecomp
   ```

3. **Ejecutar y validar los tests:**
   Asegurar que todas las suites de prueba pasen sin errores:
   ```bash
   podman run --rm -v /home/rafael/KingdomHeartsChainOfMemoriesRecomp:/src:Z -w /src localhost/khcom-builder ./build/khcom_unit_tests
   python3 tests/test_runtime_functions.py
   ```

4. **Regla de finalización:**
   Nunca responder al usuario dando por terminada una tarea de código sin haber compilado, actualizado `./KHCOMRecomp` y verificado que el binario contiene los cambios más recientes.
