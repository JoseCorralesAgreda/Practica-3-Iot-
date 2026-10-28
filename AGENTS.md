# AGENTS.md

Guía para agentes de IA que trabajan en este repositorio. Léela completa antes de planificar o escribir código.

La especificación completa está en `Práctica_MQTT.md` (fuente de verdad). Si algo de este archivo contradice la especificación, **gana la especificación** y debes avisar al usuario.

---

## 1. Idioma

| Qué | Idioma |
|-----|--------|
| Interacción con el usuario (respuestas, preguntas, planes) | Español |
| Documentación (`README.md`, `docs/`) | Español |
| Código: identificadores, comentarios, docstrings, mensajes de log | **Inglés** |
| Nombres de directorios y archivos | **Inglés** |
| Tópicos MQTT (salvo el prefijo del grupo) y campos JSON | **Inglés** |
| Mensajes de commit | Inglés |

---

## 2. Contexto del proyecto

Práctica de SIS-234 (Ingeniería de Sistemas): integrar un objeto inteligente con una app móvil y una herramienta de IA usando MQTT y MCP. Grupo 2, 3 integrantes. Entrega: jueves 29 de octubre de 2026, 17:45, enlace del repositorio GitHub por el LMS-UCB. Evaluación: Demo (1/2) y Defensa Individual (1/2). **No hay informe técnico.**

### 2.1 Decisiones ya tomadas

| Tema | Decisión |
|------|----------|
| Grupo | **2** → prefijo de tópicos `sis234/grupo2/` |
| MCU | DOIT ESP32 DevKit V1 (PlatformIO: `board = esp32doit-devkit-v1`, `framework = arduino`) |
| Sensor | HC-SR04 (5 V), alimentado a 5 V |
| Actuadores | **3 LEDs** (rojo, amarillo, verde). No hay servomotor |
| Broker | `broker.hivemq.com`, puerto 1883, **sin TLS y sin autenticación** |
| App móvil | **IoT MQTT Panel**. Es una app ya publicada: solo se configura, **no se desarrolla nada de ella en el repo** |
| Lenguajes | Firmware: C++ con PlatformIO. Servidor MCP: Python |

Consecuencias directas:

- El broker es público y sin usuario/contraseña: el **único secreto** del proyecto es la contraseña WiFi. Los tópicos deben llevar siempre el prefijo `sis234/grupo2/` y cada cliente un `client_id` único, para no interferir con otros grupos.
- **No** agregar divisor de tensión ni resistencia en el pin ECHO del HC-SR04: se verificó empíricamente que funciona sin ella y el riesgo está aceptado (no se busca un producto terminado). No lo menciones como problema ni lo agregues al diseño.
- Del lado de la app solo se entrega lo que pide la tarea: la exportación de la configuración del panel, **si la app lo permite**, en `config/`. No se crea código ni documentación de la app más allá de lo necesario en el README.

### 2.2 Componentes

1. **Objeto inteligente**: ESP32 + HC-SR04 + 3 LEDs. Es un cliente MQTT.
2. **Broker MQTT en la nube** (HiveMQ público).
3. **Aplicación móvil** (IoT MQTT Panel): cliente MQTT configurado con widgets.
4. **Servidor MCP** (Python), desarrollado por el grupo. Es un cliente MQTT y expone herramientas a la herramienta de IA (Claude Desktop).

### 2.3 Regla de arquitectura innegociable

**Todos los clientes se conectan al broker. Ningún componente se comunica directamente con el objeto inteligente.** Nada de HTTP, serial, WebSocket ni conexiones directas entre el servidor MCP, la app y el dispositivo.

---

## 3. Requerimientos que el código y el diseño deben cumplir

### 3.1 Funcionales

- **RF1.** El objeto mide la distancia con el sensor ultrasónico.
- **RF2.** El objeto publica periódicamente la distancia en un tópico del broker.
- **RF3.** El objeto está suscrito a un tópico de comandos y enciende/apaga los LEDs según los comandos.
- **RF4.** La app muestra el sensor de forma **gráfica, no textual** (línea, gauge o barra).
- **RF5.** La app envía comandos para encender/apagar los LEDs.
- **RF6.** La herramienta de IA interpreta lenguaje natural para mostrar datos del sensor y ejecutar comandos, **solo a través de las herramientas MCP**. Ejemplos que deben funcionar:
  - "¿A qué distancia está el objeto ahora?"
  - "Enciende el LED rojo y apaga los demás."

(La especificación menciona también un servomotor; este grupo usa LEDs, que cumple el mínimo de "3 LEDs o 1 servomotor".)

### 3.2 No funcionales: valores declarados

La especificación exige declarar valores medibles y verificarlos en la Demo. Valores de este grupo:

| Atributo | Valor declarado | Por qué |
|----------|-----------------|---------|
| Rango de trabajo | **5 a 200 cm**, objeto plano y perpendicular al sensor | El HC-SR04 nominal cubre 2–400 cm, pero cerca de los extremos y con superficies irregulares es poco fiable; este rango es cómodo de medir con cinta métrica y de verificar en la demo |
| Exactitud | **≤ ±3 cm** dentro del rango | Valor de referencia de la especificación; el HC-SR04 tiene resolución de ~0,3 cm, así que hay margen |
| Frecuencia de publicación | **1 lectura/s** (una cada 1000 ms) | Ignorar la referencia >= 2/s en el documento. Se tendrá solo una lectura por segundo |
| Tiempo de respuesta | **≤ 1 s** de extremo a extremo (app → broker → LED) | Valor de referencia; con un `loop()` sin bloqueos la latencia real debería ser de decenas o cientos de ms |
| Estabilidad | **≥ 10 min** de operación continua sin reinicios ni bloqueos | Valor de referencia de la especificación |
| Reconexión | WiFi y MQTT restablecidos automáticamente, **en ≤ 30 s** tras volver el servicio, sin intervención manual; reintentos cada 5 s | La especificación no da número; 30 s es verificable en la demo apagando y encendiendo el WiFi/hotspot |

Estos valores van al README. Si cambian, se actualiza el README y se avisa al usuario.

Implicaciones de diseño:

- El `loop()` del firmware **no debe bloquearse** (prohibido `delay()` largo; usar `millis()`). Un bloqueo rompe el tiempo de respuesta y la frecuencia de publicación.
- Toda lectura del sensor debe tener timeout (un eco perdido no puede colgar el firmware).
- La reconexión se hace dentro del `loop()` verificando WiFi y MQTT, con reintentos espaciados y sin bloquear el resto del programa.
- El servidor MCP no debe bloquearse esperando datos indefinidamente: usa timeouts y reporta claramente si no hay datos recientes.

### 3.3 Credenciales

- **Nunca** subir ni escribir en el código la contraseña WiFi.
- Firmware: archivo `secrets.h` real excluido en `.gitignore` + plantilla `secrets.example.h` con valores falsos.
- El broker no tiene credenciales y el servidor MCP no necesita secretos: no crear `.env` ni sistemas de configuración innecesarios; host, puerto y tópicos son constantes en el código.
- Antes de cualquier commit, verifica que no haya credenciales en los archivos modificados.

---

## 4. Principios de diseño y código

### 4.1 Sencillez primero

- **Sin overengineering.** El proyecto es acotado y no necesita ser escalable. No diseñar para cambios futuros hipotéticos.
- Prohibido: patrones de diseño sin necesidad concreta, jerarquías de herencia, interfaces abstractas con una sola implementación, plantillas/generics innecesarios, capas "por si acaso", factories, sistemas de plugins, configuración excesivamente parametrizable.
- Solo se crea una clase, módulo o archivo si un requisito actual lo justifica.
- Si una función simple resuelve el problema, no la envuelvas en una clase. (La rúbrica pide código orientado a objetos y modular: usa clases donde representen un componente real, como el sensor, los LEDs o el cliente MQTT; no más.)
- Estructura de archivos **plana y mínima**, reflejando esta sencillez.

### 4.2 Legibilidad sobre ingenio

- Código directo y explícito. Prefiere varias líneas claras a una expresión compacta pero difícil de entender.
- Evita trucos: operadores ternarios anidados, manipulación de bits innecesaria, macros complejas, metaprogramación, comprensiones o lambdas encadenadas difíciles de leer, `*args/**kwargs` sin necesidad.
- Nombres descriptivos y completos. Sin abreviaturas crípticas.
- Funciones cortas, con una sola responsabilidad.
- Comentarios (en inglés) para explicar el *porqué*, no lo obvio. Cada clase y función pública lleva documentación breve.
- Sin números mágicos: constantes con nombre (pines, intervalos, límites).

### 4.3 Guías de estilo

- **C++**: seguir las [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/) en lo aplicable a microcontroladores y las convenciones habituales de Arduino/PlatformIO. Elegir **una** convención de nombres y mantenerla en todo el firmware (clases `PascalCase`, métodos y variables `camelCase`, constantes `kPascalCase`, miembros privados con sufijo `_`). Archivos `PascalCase.h/.cpp` (uno por clase). Usar `const`, evitar memoria dinámica y `String` de Arduino en el camino frecuente (preferir buffers fijos), evitar variables globales salvo las estrictamente necesarias en `main.cpp`.
- **Python**: [PEP 8](https://peps.python.org/pep-0008/) y [PEP 257](https://peps.python.org/pep-0257/). `snake_case` para funciones/variables, `PascalCase` para clases, `UPPER_SNAKE_CASE` para constantes. Type hints en firmas públicas. Docstrings breves. Formato compatible con `ruff`/`black`.

---

## 5. Flujo de trabajo

Se trabaja **planificando primero, implementando después**. No escribas código sin un plan aprobado por el usuario.

### 5.1 Planes

1. **Plan general** (alto nivel): arquitectura, tópicos, formato de mensajes, estructura del repositorio, reparto de responsabilidades. Sin detalles de implementación.
2. **Plan del objeto inteligente** (detallado), dividido en dos partes:
   - **Parte 1: lectura de sensores y funcionamiento de actuadores** (HC-SR04 y LEDs).
   - **Parte 2: cliente MQTT** (WiFi, conexión al broker, publicación, suscripción, procesamiento de comandos, reconexión).
3. **Plan del servidor MCP** (detallado): cliente MQTT, herramientas expuestas, manejo de datos recientes, configuración en Claude Desktop.

Reglas de los planes:

- Se guardan en `docs/plans/` con nombre en inglés (p. ej. `01-general-plan.md`), redactados en español.
- Son **archivos de una sola vez**: una vez ejecutados quedan archivados y **no se actualizan**.
- Deben ser lo bastante concretos para implementarse sin decisiones pendientes (archivos, clases, métodos, pines, tópicos, formatos, orden de implementación).
- Si una decisión necesaria para cumplir la especificación no está clara, **pregunta al usuario** (ver §9) antes de asumirla.

### 5.2 Implementación

- Implementa exactamente lo que el plan indica. Si descubres que el plan es inviable o incompleto, detente y consulta; no improvises un rediseño.
- Cambios pequeños y enfocados: una unidad lógica por cambio.

### 5.3 Documentación (distinta a los planes)

La documentación se genera **después** de escribir el código y **se actualiza** cada vez que el código cambia. Es simple, legible y sin relleno. Está en español.

| Archivo | Contenido |
|---------|-----------|
| `README.md` | Breve y exactamente lo que pide la especificación: integrantes, tópicos MQTT y formato de mensajes, herramientas MCP (nombre, parámetros, propósito), valores declarados de los requerimientos no funcionales, instrucciones para ejecutar el sistema. Enlaza a `docs/` |
| `docs/firmware.md` | Cómo funciona el código del objeto inteligente (responsabilidad de cada clase, flujo del `loop()`, cómo se procesa un comando, cómo se reconecta) y **por qué** se tomaron las decisiones importantes |
| `docs/mcp-server.md` | Cómo funciona el servidor MCP (herramientas, flujo de datos desde el broker, manejo de datos antiguos) y **por qué** se tomaron las decisiones importantes |

Reglas:

- Describen con más detalle que el README el funcionamiento del código, pero **no son extensas**: sin repetir lo que el código ya dice claramente.
- Las decisiones importantes se justifican brevemente (p. ej. por qué `millis()` en vez de `delay()`, por qué se guarda la última lectura en el servidor MCP, por qué ese QoS). Esta justificación sirve para la Defensa Individual.
- No crear documentos adicionales salvo que el usuario lo pida.
- Distinción clave: `docs/plans/` son planes archivados que **no se tocan**; el resto de `docs/` son documentos vivos que **sí se actualizan**.

---

## 6. Objeto inteligente (PlatformIO, C++)

### 6.1 Desacoplamiento por integrante

Hay 3 integrantes y cada uno debe hacer commits propios. El firmware se divide en **dos partes independientes** para que dos personas trabajen el mismo plan en paralelo y produzcan commits distintos:

| Parte | Responsabilidad | Conoce MQTT/WiFi | Conoce pines/hardware |
|-------|-----------------|:---:|:---:|
| **Parte 1** | Sensor HC-SR04 y LEDs | No | Sí |
| **Parte 2** | WiFi + cliente MQTT | Sí | No |

Reglas de desacoplamiento:

- Cada parte vive en **sus propios archivos** (`.h/.cpp`), sin editar archivos de la otra parte.
- La Parte 1 **no incluye** cabeceras de WiFi ni MQTT. La Parte 2 **no toca** pines, `pulseIn` ni `digitalWrite`.
- Se comunican solo mediante una **interfaz mínima y explícita** definida en el plan (p. ej. una función para leer la distancia en cm y una para encender/apagar un LED por color), acordada *antes* de implementar.
- `main.cpp` solo une las partes (crear objetos, llamar a `begin()` y `update()`); no contiene lógica de negocio.
- Cada parte debe poder compilar y probarse por separado (la Parte 1 con salida serial; la Parte 2 con valores de prueba).
- Al planificar, indica qué parte corresponde a qué archivos para facilitar commits separados.

### 6.2 Estructura sugerida (mantener plana)

```
firmware/
├── platformio.ini
├── include/
│   ├── secrets.example.h      # plantilla sin valores reales
│   ├── DistanceSensor.h       # Parte 1
│   ├── LedController.h        # Parte 1
│   └── MqttClient.h           # Parte 2
└── src/
    ├── main.cpp
    ├── DistanceSensor.cpp
    ├── LedController.cpp
    └── MqttClient.cpp
```

`secrets.h` (real) va en `.gitignore`. Las clases exactas se definen en el plan; no agregues archivos o capas fuera del plan.

### 6.3 Notas técnicas

- Interpretar los comandos JSON y aplicarlos a los LEDs se define en el plan (la Parte 2 recibe y decodifica; la Parte 1 ejecuta). Los comandos inválidos o desconocidos se ignoran sin colgar ni reiniciar el dispositivo (log por serial).
- Filtrado de lecturas: solo el mínimo necesario para cumplir ±3 cm (p. ej. descartar lecturas fuera del rango de trabajo o con timeout). No agregar filtros complejos.
- El intervalo de publicación (200 ms) es una constante con nombre.
- Montaje ordenado y seguro (la rúbrica lo evalúa): resistencia en serie para cada LED, cableado prolijo. Los pines se proponen en el plan de la Parte 1 y se documentan.

---

## 7. Servidor MCP (Python)

- **Librería MCP:** usa la más conveniente (p. ej. el SDK oficial `mcp` con `FastMCP`), siempre que **no contravenga la documentación oficial de MCP para servidores** (modelo de herramientas, transportes, ciclo de vida). Si eliges otra, justifícalo en el plan.
- Cliente MQTT con `paho-mqtt` u otra librería simple. Dependencias mínimas, declaradas en un solo archivo (`requirements.txt` o `pyproject.toml`).
- Transporte **stdio**, para Claude Desktop. Según la documentación oficial de MCP, en un servidor stdio **nunca se debe escribir en stdout** (corrompe el protocolo); los logs van a stderr.
- El cliente MQTT se suscribe al tópico de distancia y **guarda la última lectura con su marca de tiempo**. La herramienta de lectura devuelve ese valor y avisa si es demasiado antiguo o si aún no llegó ninguno.
- Las herramientas de LEDs **publican comandos** en el tópico de comandos; **no** acceden al dispositivo por otra vía.
- Herramientas con nombres, parámetros y descripciones claros, en inglés, para que la IA las interprete bien en lenguaje natural. Valida los parámetros (solo colores existentes, estados válidos). Respuestas simples y legibles.
- Estructura mínima (ajustar al plan):

```
mcp-server/
├── server.py          # herramientas MCP
├── mqtt_client.py     # conexión y última lectura
└── requirements.txt
```

- Incluir en `config/` la configuración del servidor MCP para Claude Desktop (p. ej. `claude_desktop_config.example.json`, con rutas genéricas, sin datos personales).

---

## 8. MQTT: lineamientos

- Broker: `broker.hivemq.com:1883`, sin TLS ni autenticación.
- Prefijo obligatorio en todos los tópicos: `sis234/grupo2/`. Tópicos propuestos (se confirman en el plan general): `sis234/grupo2/distance` y `sis234/grupo2/leds/cmd`.
- Los nombres de tópicos y el formato de mensajes son decisión del grupo, definidos en el plan general y documentados en el README. Los de la especificación son solo ilustrativos.
- Formato de mensajes: JSON simple y plano (p. ej. `{"cm": 42.7}`). Un solo formato consistente en firmware, app y servidor MCP. Debe poder ser leído y generado por IoT MQTT Panel (widgets con payload JSON o texto simple); verifícalo al definir el formato.
- Los tópicos y formatos del plan general son el **contrato** entre firmware, app y servidor MCP: no cambiarlos sin actualizar los tres y avisar al usuario.
- Cada cliente usa un `client_id` distinto y único (en un broker público, dos clientes con el mismo id se desconectan entre sí). Incluye el prefijo del grupo en el id.
- QoS: elegir niveles simples y justificarlos en `docs/` (la Defensa evalúa MQTT: modelo pub/sub, broker, tópicos y QoS). Valor de partida razonable: QoS 0 para distancia, QoS 1 para comandos.

---

## 9. Cuándo preguntar al usuario

Pregunta (de una en una, con propuesta y justificación breve) cuando la respuesta afecte el cumplimiento de la especificación y no esté resuelta en este archivo. Las decisiones de la §2.1 y los valores de la §3.2 **ya están tomados**: no las vuelvas a preguntar.

Todavía abiertas (se proponen en los planes y el usuario las confirma):

- Asignación de pines del ESP32.
- Formato exacto del mensaje de comandos de los LEDs, compatible con IoT MQTT Panel.

No preguntes por detalles menores que puedas decidir siguiendo los principios de este archivo.

---

## 10. Git y repositorio

- Cada integrante usa su propia cuenta de GitHub: el historial de commits evidencia su aporte en la Defensa Individual.
- **No hagas commits ni push por tu cuenta** salvo que el usuario lo pida explícitamente. No modifiques la autoría.
- Commits pequeños, enfocados y con mensaje claro en inglés. Una parte del plan por commit cuando sea posible (así las partes 1 y 2 del firmware quedan en commits separados).
- `.gitignore` debe excluir: `secrets.h`, `.pio/`, `__pycache__/`, entornos virtuales y archivos de IDE.
- El repositorio final contiene solo: código documentado del firmware, código documentado del servidor MCP, configuraciones (plantilla del firmware, configuración MCP, exportación del panel de IoT MQTT Panel si es posible), `README.md`, `docs/` y los planes archivados.

### Estructura general del repositorio

```
.
├── AGENTS.md
├── README.md
├── .gitignore
├── docs/
│   ├── firmware.md
│   ├── mcp-server.md
│   └── plans/              # planes archivados, no se actualizan
├── config/                 # config de Claude Desktop y export del panel de la app
├── firmware/
└── mcp-server/
```

---

## 11. Lista de verificación antes de dar algo por terminado

- [ ] Toda comunicación pasa por el broker; no hay conexiones directas.
- [ ] RF1–RF6 cubiertos y demostrables.
- [ ] Todos los tópicos usan el prefijo `sis234/grupo2/`; los `client_id` son únicos.
- [ ] Firmware sin bloqueos; reconexión WiFi y MQTT automática.
- [ ] Sin la contraseña WiFi en el repositorio; plantilla de ejemplo presente.
- [ ] Código en inglés; sin overengineering; sin trucos difíciles de leer.
- [ ] Partes 1 y 2 del firmware desacopladas, en archivos separados.
- [ ] Nada se imprime a stdout en el servidor MCP.
- [ ] `README.md`, `docs/firmware.md` y `docs/mcp-server.md` actualizados con el código actual.
