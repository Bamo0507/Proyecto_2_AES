# Búsqueda de claves AES

Proyecto del curso **CC3169 — Computación Paralela y Distribuida**, Universidad
del Valle de Guatemala. Integrantes: Adriana Palacios, Bryan Martínez y Daniela
Ramírez.

El proyecto se organiza por enfoque de implementación, con módulos compartidos
para reutilizar el código común.

## Organización

| Ruta | Contenido |
| --- | --- |
| `src/sequential/` | Implementaciones secuenciales, incluida la versión original como referencia. |
| `src/distributed/` | Implementaciones MPI: reparto cíclico y búsqueda por bloques desde archivo. |
| `src/common/` | Código compartido entre las implementaciones. |
| `tests/` | Pruebas para verificar el funcionamiento del código. |
| `scripts/` | Scripts de apoyo para automatizar tareas del proyecto. |
| `docs/` | Documentación, propuestas y diagramas. |
| `results/` | Resultados de los experimentos. |
| `build/` | Archivos generados durante la compilación. |
| `Makefile` | Configuración de compilación y tareas de verificación. |
