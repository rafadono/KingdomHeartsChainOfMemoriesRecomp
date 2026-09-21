# Simbolos de Kingdom Hearts: Chain of Memories (B8CE)

Este directorio contiene las definiciones y mapeos de simbolos utilizados por el recompilador estatico (`gba_recompile`).

## Estructura de archivos

- `imported_symbols.tsv`: Direcciones de funciones mapeadas con su modo (ARM o THUMB) y nombre de funcion C.
- `khcom_syms.txt`: (Opcional) Volcado `readelf -sW` de la compilacion de decompilacion (`pheenoh/khcom`).
- `khcom_sections.txt`: (Opcional) Volcado `readelf -SW` de secciones.
- `khcom.map`: (Opcional) Archivo de mapa de enlace generado por `ld -Map`.

## Importacion automatica de simbolos

Para importar la totalidad de nombres de funciones desde el proyecto de decompilacion matching de Kingdom Hearts: Chain of Memories (`pheenoh/khcom`), ejecute:

```powershell
python tools/import_decomp_symbols.py
```
