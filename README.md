# Búsqueda de claves AES

Proyecto del curso **CC3169 — Computación Paralela y Distribuida**, Universidad
del Valle de Guatemala. Integrantes: Adriana Palacios, Bryan Martínez y Daniela
Ramírez.

El código está separado según la forma de buscar la clave. Las partes que usan
varias versiones se reúnen en una misma carpeta para evitar repetirlas.

## Organización

| Carpeta o archivo | Qué contiene |
| --- | --- |
| `src/sequential/` | Versiones que prueban las claves una por una con un solo trabajador. Incluye el ejemplo original. |
| `src/distributed/` | Versiones que reparten la búsqueda entre varios trabajadores mediante MPI. |
| `src/common/` | Funciones que comparten las versiones, como cifrar un mensaje o revisar una posible clave. |
| `tests/` | Pruebas para comprobar que los programas funcionan como esperamos. |
| `scripts/` | Programas de apoyo para repetir pruebas y guardar sus resultados. |
| `guides/` | Guías compartidas para organizar el código y medir las búsquedas. |
| `docs/` | Planes, propuestas y diagramas locales que se mantienen fuera de Git. |
| `results/` | Resultados de las pruebas y comparaciones. |
| `build/` | Archivos que se crean al preparar los programas para ejecutarlos. |
| `Makefile` | Instrucciones para preparar los programas y ejecutar las pruebas. |
