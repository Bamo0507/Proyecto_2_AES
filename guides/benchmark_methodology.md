# Cómo medir y comparar las búsquedas

Esta guía explica qué se mide y cómo guardar los resultados. Las pruebas de
funcionamiento comprueban que el código responda correctamente; las mediciones
permiten comparar cuánto tarda cada forma de buscar.

## Comparación del proyecto

La referencia es `aes_search_file`, que busca con un solo trabajador. Se compara
con `aes_search_mpi_blocks` y `aes_search_mpi_hybrid`. Las tres versiones leen el
mismo archivo y usan las mismas funciones para construir claves, descifrar el
mensaje completo y buscar la frase `es una prueba de`.

Mantener el mismo mensaje, número de clave, cantidad de bits y opciones de
compilación. Para comparar valores de k, conservar también la semilla. El dato
de inicio del cifrado se genera de nuevo en cada ejecución; se compara el mismo
tipo de ejercicio, sin exigir que los mensajes cifrados sean idénticos.

La clave del ejercicio solo se usa al preparar el mensaje cifrado. La búsqueda
prueba claves y comprueba la frase sin consultar esa respuesta.

```sh
python3 scripts/compare_distributed.py --bits 20 --key 1048575 --processes 4 --repetitions 5 --k 8 16 32 --seed 1
```

El comparador prepara una compilación propia y hace una ejecución inicial de cada
variante que no cuenta entre las mediciones. Después repite las búsquedas y cambia
cuál se ejecuta primero. Guarda en una carpeta dentro de `results/runs/`:

- `measurements.csv`: cada tiempo y cantidad de claves probadas, con su variante,
  cantidad de trabajadores, valor de k y repetición.
- `summary.csv`: las medianas, el speedup y la eficiencia. La mediana es el valor
  central de los tiempos ordenados.
- La salida de cada búsqueda, las opciones de compilación, los datos de la máquina,
  una copia del mensaje y una copia del código y las guías utilizados.

## Qué tiempo se cuenta

La medición comienza después de leer y cifrar el mensaje. En secuencial termina
al encontrar una coincidencia o agotar las claves. En MPI hay una barrera antes
de empezar, para que los trabajadores arranquen juntos, y se toma la duración
del que más tardó.

El tiempo de MPI incluye las pruebas y los avisos usados para terminar la
búsqueda. En la híbrida se completan también los envíos y las recepciones pendientes.
El recorrido híbrido se pausa cada cierto número de cálculos para revisar los
avisos, incluido el trabajo de descartar claves repetidas. Esas pausas forman
parte del tiempo de búsqueda y conservan el orden de las claves.
Quedan fuera el arranque de `mpirun`, la preparación del mensaje, las estadísticas
reunidas al final y la impresión de los resultados.

Cada trabajador mide una duración con su propio reloj. No se restan horas de
máquinas distintas. Las versiones secuenciales usan `CLOCK_MONOTONIC`; MPI usa
`MPI_Wtime()`.

## Speedup y eficiencia

El speedup indica cuántas veces fue más rápida la búsqueda con varios trabajadores
respecto a la referencia secuencial. La eficiencia divide esa mejora entre la
cantidad de trabajadores que participaron.

```text
Speedup(p)    = mediana del tiempo secuencial / mediana del tiempo con p trabajadores
Eficiencia(p) = Speedup(p) / p
Porcentaje   = 100 * Eficiencia(p)
```

El comparador muestra la eficiencia como porcentaje y la guarda en `summary.csv`
como proporción: por ejemplo, `0.75` significa 75 %. Para la referencia secuencial,
el speedup y la eficiencia valen 1.

Conservar los conteos de claves probadas: al encontrar una coincidencia, otros
trabajadores pueden hacer pruebas adicionales antes de enterarse. La ubicación
de la clave y el orden de búsqueda también cambian la cantidad de trabajo.

## Claves del enunciado

Las pruebas permanentes usan cuatro procesos, 56 bits y las claves `123456`,
`18014398509481984` y `18014398509481985`. Se ejecutan con:

```sh
make test
make test-mpi-blocks
make test-mpi-hybrid
```

Las dos claves grandes pueden encontrarse pronto con cuatro trabajadores porque
caen al inicio del grupo del trabajador 1. Una búsqueda secuencial empieza en cero
y tendría que avanzar hasta ellas; su medición puede ser impracticable.

Para comparar solo las dos estrategias MPI en esos casos se puede usar:

```sh
python3 scripts/compare_distributed.py --bits 56 --key 18014398509481984 --processes 4 --repetitions 5 --skip-sequential
```

Sin la medición secuencial, los campos de speedup y eficiencia quedan vacíos y la
salida indica `sin referencia`. No sustituir ese tiempo por una estimación ni
presentar la comparación entre dos estrategias MPI como un speedup secuencial.

## Antes de interpretar los resultados

Usar compilaciones con las mismas opciones de optimización, sin detectores de
errores de memoria. Registrar CPU, núcleos físicos, ubicación de los procesos,
red si hay varias máquinas y otras tareas que se estuvieran ejecutando. Evitar
poner más trabajadores que recursos disponibles al medir rendimiento.

Repetir los casos y conservar la variación entre sus tiempos. Una búsqueda muy
corta puede estar dominada por el ruido del reloj y del sistema; los casos de
prueba pequeños sirven para comprobar el funcionamiento.
