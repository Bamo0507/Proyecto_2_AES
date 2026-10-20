# Módulos compartidos

Esta carpeta reúne las operaciones que utilizan tanto la búsqueda secuencial CTR
como la distribuida con MPI. Cada ejecutable define el recorrido de candidatas,
la medición del tiempo y su forma de terminar ante un error. La referencia ECB
se mantiene independiente.

| Módulo | Responsabilidad | Datos principales |
| --- | --- | --- |
| `search_options` | Validar argumentos y establecer los límites del ejercicio. | `SearchOptions`: bits desconocidos, rango, candidata secreta y mensaje conocido. |
| `aes_ctr` | Preparar el mensaje cifrado, construir claves y verificar candidatas. | `AesCtrContext`: recursos EVP. `AesSearchProblem`: base, IV y texto cifrado. |
| `search_reporting` | Descifrar con la candidata elegida y mostrar las estadísticas. | `SearchResult`: candidata encontrada, pruebas realizadas y duración. |

Los archivos `.h` documentan el uso de cada función, sus parámetros, condiciones
previas y retornos. Los `.c` explican las decisiones internas, como el orden de
los bytes de una clave y la reinicialización del contador CTR.

## Flujo de uso

1. `search_options_parse()` valida la entrada. Establece el rango exclusivo
   `[0, total_candidates)`, donde `total_candidates = 2^unknown_key_bits`.
2. Cada trabajador inicializa `AesCtrContext` con `{0}` y llama a
   `aes_ctr_context_open()`. La instancia es suya y no se comparte entre hilos.
3. `aes_ctr_prepare_problem()` genera una base de clave y un IV aleatorios y cifra
   el mensaje con la candidata secreta. En MPI lo hace únicamente rank 0; el
   ejecutable distribuye los arreglos antes de comenzar la búsqueda.
4. El ejecutable llama a `aes_ctr_test_candidate()` para cada valor asignado.
   Esa función construye la clave y comprueba el prefijo conocido; no consulta
   la candidata secreta para decidir si existe coincidencia.
5. Al terminar la búsqueda y la medición, `search_report()` recibe el resultado.
   En MPI se llama desde rank 0 después de agregar conteos y tiempos.
6. Cada trabajador libera sus recursos con `aes_ctr_context_close()`.

## Retornos y propiedad de memoria

Las funciones que pueden fallar devuelven `1` en éxito y `0` en error. En
`aes_ctr_test_candidate()`, ese retorno indica si se pudo hacer la evaluación:
el parámetro de salida `matches_prefix` indica si la candidata coincide.

Los módulos no llaman a `exit()` ni a `MPI_Abort()`. El ejecutable decide si
libera recursos y termina o si debe abortar el comunicador para evitar que otros
procesos queden esperando. Los errores EVP pueden consultarse con
`ERR_print_errors_fp()`. Los errores de argumentos se escriben en el flujo
entregado a `search_options_parse()`, que puede ser `NULL` para silenciarlos.

`SearchOptions.message` referencia el argumento original o un literal y no debe
liberarse desde estos módulos. Las claves, el IV y el texto cifrado se guardan
en arreglos de tamaño fijo; sus longitudes se expresan en **bytes**. Las salidas
EVP son binarias y no tienen terminador nulo. Los buffers de transformación deben
tener capacidad `AES_OUTPUT_CAPACITY`, aunque el mensaje sea más pequeño.

## Supuestos que deben conservarse

- Solo se reemplazan los bits inferiores de la clave base. La cola se interpreta
  como entero big-endian para obtener la misma clave en distintas arquitecturas.
- Cada candidata comienza con la misma base y el mismo IV del problema; no se
  continúa el contador que dejó la operación anterior.
- Se comparan como máximo 8 bytes conocidos. Una coincidencia indica una clave
  compatible con ese prefijo; no demuestra que sea la candidata secreta original.
  Los mensajes cortos ofrecen menos evidencia y admiten más coincidencias.
- `AES_CANDIDATE_NOT_FOUND` representa la ausencia de coincidencia y queda fuera
  del rango de candidatas permitido.
- El proceso ganador que imprime `search_report()` se deduce del reparto cíclico
  actual mediante `candidata % process_count`. Otro reparto requiere revisar ese
  cálculo.
- El reporte recibe el tiempo ya medido: en MPI se usa el máximo de las duraciones
  locales y la suma de las candidatas probadas. No incluye su propio descifrado
  final ni la impresión en el intervalo de búsqueda.
