# Cómo organizamos y escribimos el código

Estas reglas ayudan a reconocer qué hace cada archivo y a mantener juntas las
operaciones que necesitan varias versiones del programa.

## Archivos y nombres

- Separar las versiones según su forma de buscar: un trabajador en
  `src/sequential/` y varios trabajadores en `src/distributed/`.
- Guardar lo que comparten en `src/common/`. Usar nombres que expliquen su función,
  como `file_search`, en lugar de reunir tareas distintas en un archivo `utils`.
- Escribir los nombres en inglés y separar sus palabras con guiones bajos:
  `candidate_position`, `message_length`, `elapsed_seconds`. Indicar las unidades
  cuando ayuden a entender el dato.
- Escribir las constantes en mayúsculas, con el nombre de la parte a la que
  pertenecen: `AES_KEY_LENGTH` o `FILE_SEARCH_MAX_MESSAGE_LENGTH`.
- El archivo `.h` explica qué funciones ofrece una parte y cómo usarlas. El `.c`
  contiene sus pasos. Incluir primero el `.h` propio y evitar incluir archivos `.c`.
- Usar `static` para las funciones que solo se necesitan dentro de un archivo.

## Partes compartidas

La referencia secuencial de archivos y las versiones MPI por bloques e híbrida
usan `file_search` para leer el mensaje, convertir un número en una clave de 16
bytes, cifrar y comprobar la frase. Así se compara la forma de buscar manteniendo
el mismo trabajo por clave. Las versiones anteriores se conservan como ejemplos.

Las búsquedas secuenciales que comparten módulos consultan el mismo reloj mediante
`search_timing`. Los programas con MPI usan su propio reloj y reúnen las duraciones
al terminar. El ejemplo ECB original conserva sus funciones para poder estudiarlo
por separado.

## Memoria y errores

Cada trabajador prepara sus propios datos de OpenSSL una vez y los reutiliza.
`AesCtrContext` se crea con `{0}`, se abre antes de cifrar y se cierra al terminar.
No se copia una instancia abierta ni se comparte entre tareas simultáneas.

Los comentarios de cada función deben explicar qué recibe, dónde guarda su
respuesta, cuánto espacio necesita y cómo avisa si falla. Los textos tomados de
los argumentos del programa siguen perteneciendo a esa entrada; no se liberan
como si fueran copias nuevas.

Usar `size_t` para tamaños en memoria y `uint64_t` para números de clave. Antes de
convertir tamaños a los enteros que esperan OpenSSL o MPI, comprobar sus límites.
Las versiones que comparan el inicio del mensaje aceptan hasta 256 bytes; las
que leen archivos aceptan hasta 4096.

Revisar que los argumentos numéricos contengan solo dígitos, que quepan en el tipo
usado y que estén dentro del rango permitido. Comprobar el resultado de la
lectura, del cifrado y de las reservas de memoria.

Las funciones de `src/common/` devuelven un resultado para que quien las llama
pueda decidir cómo terminar. En MPI, si un error impide continuar, se detiene a
todos para que nadie quede esperando. Todos los trabajadores deben participar
en los intercambios colectivos en el mismo orden, incluso si no recibieron claves.

## Comentarios y pruebas

Los comentarios deben explicar decisiones que no se entienden solo con los nombres.
Evitar repetir cada instrucción. Los ejemplos y los README deben usar lenguaje
directo y explicar las palabras técnicas cuando hagan falta.

Compilar con las advertencias del `Makefile` y ejecutar las pruebas que correspondan.
Comprobar resultados conocidos de cifrado, la conversión de claves, los tamaños
límite, los errores de entrada y que todos los trabajadores puedan terminar.
Las herramientas que detectan errores de memoria sirven para revisar el programa;
sus tiempos no se usan para comparar rendimiento.

## Fuentes que se usaron para definir estas reglas

- [Wilson et al., Good enough practices in scientific computing](https://journals.plos.org/ploscompbiol/article?id=10.1371/journal.pcbi.1005510).
- [The Carpentries: Code and Software](https://carpentries-lab.github.io/good-enough-practices/03-software.html).
- [SEI CERT DCL15-C: funciones y datos de uso interno](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/recommendations/declarations-and-initialization-dcl/dcl15-c/).
- [SEI CERT ERR34-C: conversión de texto a números](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/error-handling-err/err34-c/).
- [GCC: advertencias del compilador](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html).
- [OpenSSL 3: funciones de cifrado EVP](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/).
- [MPI Forum: orden correcto de las operaciones colectivas](https://www.mpi-forum.org/docs/mpi-4.1/mpi41-report/node172.htm).
- [GNU Make: dependencias entre archivos](https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html).
