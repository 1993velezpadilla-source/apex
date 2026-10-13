# APEX OPTO 2A — AUDITORÍA DE CIRCUITO REAL / GOLDEN REFERENCE
**Fecha:** 2026-10-13 · **Objetivo:** hardware LA-2A y comportamiento UAD LA-2A Leveler Collection, solo el **sonido/circuito**, no la apariencia. **Estado:** INVESTIGACIÓN CON FUENTES PRIMARIAS; **ningún resultado 1:1 verificado**.

## FUENTES, CONFIANZA Y REVISIÓN
- [S1 **PRIMARIA, 1966**] Manual original Teletronix LA-2A, Library of Congress. Diagrama funcional p.8 y esquema real p.9; sección teoría p.3–4; R37 p.7. https://tile.loc.gov/storage-services/master/mbrs/recording_preservation/manuals/Teletronix%20Model%20LA-2A%20Leveling%20Amplifier.pdf
- [S2 **PRIMARIA, REISSUE**] Universal Audio hardware LA-2A manual, sección Circuit Description, diagramas de entrada/T4 y driver. https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf
- [S3 **PRIMARIA, PLUGIN**] UA LA-2A Leveler Collection manual (Silver/Gray/LA-2/Legacy, Reference Level, Peak, HF/R37, Mix). https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual
- [S4 **PRIMARIA, FÁBRICA PLUGIN**] UA anuncio 2013: colección nueva modela amplificadores de válvulas, transformadores, T4 con nonlinearidades/mejores constantes; Legacy era más limitada. https://www.uaudio.com/blogs/press/la-2a-collection
- [S5 **PRIMARIA, FABRICANTE**] Historia y T4 desde Universal Audio. https://help.uaudio.com/hc/en-us/articles/215779663-A-History-of-the-Teletronix-LA-2A-Leveling-Amplifier
- [S6 **REPARACIÓN DE HARDWARE**] Vintage King inspeccionó múltiples revisiones, documenta T4A/T4B, HA-100X/A-10 y variaciones por serial (cifras aproximadas). https://vintageking.com/pages/teletronix-la-2a-optical-compressor-limiter
- [S7 **AES PEER-REVIEWED 2026**] Austin Moore, "Objective Analysis and Perceptual Evaluation of LA-2A Compressors and Vocal Recordings", seis unidades hardware, 17 oyentes ABX. Manuscrito público https://pure.hud.ac.uk/ws/portalfiles/portal/140787498/AAM.pdf ; abstract https://pure.hud.ac.uk/en/publications/objective-analysis-and-perceptual-evaluation-of-la-2a-compressors-2/
- [S8 **PUBLIC RESEARCH/CODE**] Yu & Fazekas, 4A2A, feed-forward parameter optimization with Newton–Raphson, https://arxiv.org/abs/2509.10706 and https://github.com/aim-qmul/4a2a (MPL-2.0, review license before incorporation).
- [S9 **PUBLIC RESEARCH**] Yin et al., S4 state-space optical LA-2A https://arxiv.org/abs/2403.16331 ; optical selective state-space https://arxiv.org/html/2408.12549v1
- [S10 **USUARIOS, ANECDÓTICO**] Propietario de dos hardware LA-2A y UAD dice diferencias mínimas; otros prefieren Softube/NEOLD. https://www.reddit.com/r/audioengineering/comments/1i7y50c/ . Opiniones NO prueban superioridad estadística.

## A. AUTORIDAD ELECTRÓNICA: LEER SCHEMATIC, NO COPIAR DIAGRAMAS DE MARKETING

**Diagrama funcional del manual Teletronix 1966 p.8:**

```text
                    +-- [Tap de entrada] -> red de énfasis R37 / C12
                    |                  -> Peak Reduction R2
[INPUT] -> [T1 IN] -+                  -> 12AX7 V3 (amplificador de control)
                    |                  -> control estéreo R3 -> 6AQ5 V4
                    |                                        -> panel EL (T4A)
                    +-> [T4A CdS -> atenuación pasiva] -> Gain R1
                                                     -> 12AX7 V1 -> 12BH7 V2
                                                     -> T2 output -> [OUTPUT]
                                    (amplificador valvular V1/V2 posee
                                     ~20 dB de REALIMENTACIÓN NEGATIVA)
                 T4A también incluye una segunda CdS para medición GR.
```

**Corrección MUY importante a nuestra explicación anterior:** una cosa es la **realimentación negativa del amplificador de audio** (documentada ~20 dB) y otra el **punto de toma del detector del compresor**. En el esquema Teletronix de 1966, la rama de sidechain se separa **después del transformador de ENTRADA, antes del atenuador T4A**. Por tanto NO hay sustento para poner ciegamente un detector post-compresión por llamar a toda LA-2A "feedback compressor". Fuentes secundarias se contradicen; el esquema de la *revisión concreta* manda. Debemos validar el nodo/versión elegida antes de modelarla.

**Revisión de los bloques y dependencias:**

| Ref física | Papel eléctrico correcto | Modelado propuesto | Evidencia |
|---|---|---|---|
| T1, UTC HA-100X en unidades tempranas; A-10 tardías | transformación/impedancia AC, parásitos, posible saturación | netlist acoplado equivalente: inductancia magnetización, pérdida cobre, fuga, capacitancia, curva no lineal calibrada; no inventar parámetros | S1/S2/S6 |
| R5/R6 (68 kΩ en figura del UA reissue) y T4 | divisor resistivo de atenuación **antes de V1**; CdS reduce R al recibir más luz | divisor exacto con impedancias de fuente y carga, dos port; actualizar R por fotoestado, no multiplicar por un gainDb arbitrario | S2 |
| EL phosphor + CdS T4A/T4B | panel electroluminiscente AC, DOS CdS (audio + VU), respuesta no instantánea, memoria por exposición | (L(V,f)), photo-state rápido/lento, (R_{CdS}(L,H)), calibrar simultáneamente estática/dinámica y canal VU | S1/S2/S5 |
| R1 Gain (~100 kΩ en esquema UA) | control en recorrido de audio entre T4 y V1 | red de ganancia, su loading y calibración en dBu/dBFS, no solo multiplicación digital final | S2 |
| V1 12AX7 | etapa amplificación de voltaje, polarización y realimentación ~20 dB | ecuaciones triode de corriente de placa/cátodo + red RC y estabilidad; iteración Newton solver fuera de RT durante calibración / WDF en RT | S1/S2 |
| V2 12BH7 | follower/double cathode follower de salida | parámetros válvula + cargas, impedancia y respuesta a transitorios | S1/S2 |
| T2 UTC A-24 original (según esquemas) | output transformer 600 Ω nominal | equivalente magnético y fuga/resonancias; calibrar THD vs nivel y banda pasante | S1/S2 |
| V3 12AX7 | amplificador control/sidechain | respuesta de realimentación local, polarización y no linealidad del lado key | S1/S2 |
| R2 Peak Reduction (~100 kΩ en figura UA) | regula **ganancia de sidechain**; cambia umbral efectivo | mapa eléctrico/medido de control, no `threshold = -0.4*knob` | S1/S2 |
| V4 6AQ5 / 6AQ5A | driver de voltaje AC del fósforo EL T4 | etapa pentodo + carga capacitiva/nolineal EL, no detector RMS+pow solamente | S1/S2 |
| R37 + C12 | red pre-énfasis selectiva del SIDECHAIN | reconstruir transferencia a partir de R/C, magnitud/fase, sensibilidad dependiente de frecuencia | S1/S2/S3 |
| R3 stereo adjust | balance de reducción para dos unidades vinculadas | modelo 2 mono T4 independiente y enlace común calibrable o modo ideal linked, documentar diferencia | S1 |
| segunda CdS/VU | medidor mecánico 0 VU GR, +4/+10 salidas | balística física VU y segundo CdS desacoplados de render audio | S1/S2 |
| power supply/R-C/impedancia | ripple, headroom, coupling, limitaciones eléctricas | solver offline de netlist + proyección RT de estado, noise/hum opcionales; no inventar ruido como "calidez" | S1/S2 |

**Correcciones documentales:** El T4 usa un **panel electroluminiscente**, no lámpara incandescente/tungsteno del diseño anterior. El esquema primario y UA explican cómo *más luz disminuye resistencia CdS*. Ciertas descripciones secundarias contienen errores/inversiones; nunca usar textos de blogs como fuente de física sin comprobar S1/S2.

## B. VARIANTES ESTUDIADAS = REFERENCIAS, NO PRODUCTOS APEX

**DECISIÓN DEL ARTISTA 2026-10-13:** Crear exactamente **UN SOLO PLUGIN NATIVO APEX Opto 2A**, no tres versiones LA-2A como UAD, ni tres modos/presets escondidos que impliquen tres emulaciones. Afinarlo por medición usando su **UAD LA-2A Gray como referencia sonora primaria para voz principal**, y consultar sus **UAD Silver y LA-2 solamente como controles de contraste** que revelen el rango de comportamiento de los circuitos; no fusionar indiscriminadamente sus curvas ni hacer tres procesadores.

**Producto único:** un carácter tonal establecido; Peak Reduction / Gain / Compress-Limit / Emphasis físico cuando esté validado / Mix paralelo / medidor. NO selector Gray–Silver–LA-2. La interfaz adopta identidad APEX, nunca skins UAD. El dueño tiene UAD LA-2As, pero ninguna referencia procesada ni captura del plugin está disponible al repositorio en este momento.

### Evidencia histórica de los distintos circuitos

| Variante física/referencia | Rasgos corroborados | Objetivo APEX |
|---|---|---|
| Teletronix LA-2 / early 1960s | anterior al LA-2A, T4 historial/envejecimiento, respuesta más lenta; modelo UAD LA-2 suele ser más "mellow" | fase 3 |
| LA-2A Gray ~1962-1965 | temprano, T4A, HA-100X habitual; R37 no en TODAS las series, revisar serial | **referencia primaria vocal (UAD Gray)**; medir contra licencia instalada por el usuario |
| LA-2A Silver ~1965-1969 | varias rev.; al final T4B y A-10 en algunas; respuesta UAD Silver más rápida que Gray | comparativa solamente; NO segunda variante de APEX |
| UREI 1979/1992 reissue | A-10 y T4B frecuentes | circuito/mediciones opcionales |
| UA hardware reissue ~2000+ | versión con custom HA-100X-like, T4B | referencia electrónica accesible alternativa |
| UAD LA-2A **Legacy** | UA confirma que **NO** modela distorsión I/O y transformador | NO usar como única referencia de 1:1 |
| UAD LA-2A **Leveler Collection 2013+**: Silver/Gray/LA-2 | fabricante afirma modelado de camino electrónico completo, incluyendo válvulas/transformadores y T4 más detallado. Procesamiento PROPIETARIO; código interno no público | referencias externas licenciadas por el usuario; **Gray es target primario**, Silver/LA-2 validan contexto, ninguno se publica como variante APEX |

La "1:1 UAD" es **un OBJETIVO comparativo**, no una garantía ni algoritmo obtenido. Diferencias reales entre unidades, patentes/algoritmos propietarios, clipping y tolerancias impiden inferir que un único ajuste represente *todas* las LA-2A. No copiar código, presets, GUI, logos, datos protegidos ni empaquetar modelos de terceros sin revisar licencia.

## C. DATA EXPERIMENTAL PUBLICADA QUE CAMBIA EL PLAN

**Austin Moore, AES 2026, seis LA-2A hardware (3 Teletronix, 3 UA reissues):**
- Attack "hasta estabilizar" **33–81 ms**, average ~52.8 ms; release "hasta estabilizar" **449–1670 ms**, average ~916.5 ms. Son definiciones de estabilización (derivada por debajo de umbral) y NO contradicen 10 ms attack nominal ni 60 ms 50%-release, que son métricas DIFERENTES.
- THD a 1 kHz observada ~**0.94% a 4.22%** entre algunas unidades, y depende fuertemente del nivel de prueba; no fijar un tanh para todas las condiciones.
- A/B/X 17 oyentes; algunas discriminaciones significativas, otras no, contextuales; mayor diferencia relativa entre una pareja a ~3 dB GR. Las unidades vintage **no** forman un cluster inequívoco contra reissues. Ver S7.
- Metodología de ensayo incluye interfaz de referencia loopback (fundamental) y precalentamiento del T4; se recomienda reproducir protocolos, no usar solo un seno.

**Implicación:** no "calibrar" al azar con varios ms fijos, ni medir solo reducción en 6dB. Determinar funciones de pérdida separadas para respuesta espectral, dinámica larga, distorsión y escucha normalizada.

## D. LECTURA DIRECTA DEL PROTOTIPO APEX / DEUDA TÉCNICA

- `Source/CompressorCore/ApexOptoLevelerCore.h` implementa `thresholdDb = -(peakReduction*0.40)`, ratio/compress/limit con rodilla genérica, 10 ms ataque y dos polos de release + memoria; NO simula el R2 ni la curva T4 física.
- `scLowpassCoeff_ ... 320Hz` representa una aproximación HP shelf del key; NO valida R37/C12 original.
- `color()` con `tanh()` es color deliberadamente ficticio; no hay modelo de 12AX7/12BH7/6AQ5 ni de T1/T2 y no hay oversampling. Control `ColorEnabled` está **OFF por defecto en el processor**, aunque el struct DSP default sea true.
- `ApexOptoLevelerProcessor.h` ya tiene sliders/estado y dry-wet digital; eso NO demuestra que el componente insert se comporte como UAD.
- CI portátil testea invariantes DSP; la compilación Windows full puede fallar por tests JUCE ajenos. Reparar fallos del host gate antes de certificar.
- La implementación actual limita audio a [-8,8] al sanitizar entradas y controla color al output; ese límite puede afectar pruebas de clipping/extremos de 1:1. Debe sustituirse por un circuito con headroom calibrado.

## E. PLAN DE MODELO FÍSICO REAL-TIME (CIRCUIT-FIRST, MEASURED-FIT)

1. **CircuitGraph de revisión declarada**: almacenar topología identificada, nodos, T1/T2, R/C, válvulas, T4 y ramas key. Guardar serie/referencia, BOM y tolerancias. No "vector de constantes mágicas".
2. **Referencia offline SPICE/MNA** para verificar nodos, impedancia, polarización y lazo de realimentación del amplificador; derivar un modelo de ejecución determinista de **estados limitados**, prealocar y calibrar por respuesta de frecuencia/THD. Las tensiones físicas extremas nunca se entregan al usuario.
3. **EL + LDR T4**: dos estados fotoquímicos con memoria no lineal, curvas R(flujo lumínico, historia) y rama independiente de medidor; probar burst 1 ms a >=20 s, pausa 60ms/500ms/5s/15s y calentamiento/pre-carga.
4. **Tubos**: solver triode/pentodo parametrizado V1/V2/V3/V4; validar transferencias DC y AC, clipping suave/asimétrico y espectro armónico a diferentes niveles; evitar solo `tanh` global. Usar Wave Digital Filter / statespace con seguridad CPU.
5. **Transformadores**: acoplamiento, inductancia no lineal, histéresis si es identificable con datos, carga nominal; alias/harmonics con oversampling interno medido 2x/4x/8x, anti-alias.
6. **Backend dual**: low-latency live causal para 48kHz/64-128 samples, y opción HQ offline; PDC/latencia anunciada, bypass sin clic, paralelo dry/wet alineado en ambos modos. CPU cuantificado y no alocar nunca en callback.
7. **Factory voice test**: APEX Opto 2A comparado primero con **Gray** en la misma señal vocal; Silver/LA-2 se prueban en ensayos de contraste, no se reproducen como variantes. NO autoajustar loudness basándose en factor de compresión. Input y Output GR +4/+10 con balística fiel, calibración -12 dBFS de **UAD Collection** como referencia del plugin, sin confundir con +4dBu de hardware.

## F. PROTOCOLO A/B GOLDEN WAV — SIN ADIVINAR CÓDIGO UAD

1. Golden device: hardware unit con identidad/estado/control/temperatura, o plugin UAD Collection licenciado con versión+host+sample rate; evitar Legacy para la meta completa.
2. Enviar exactamente las mismas señales a referencia y APEX; fijar **Gain**, **Peak Reduction**, **Compress/Limit**, **R37** y calibración. Documentar entrada, salida y etapa de normalización.
3. Crear fixture inputs: 30, 63, 125, 250, 500, 1k, 3k, 10k, 15k Hz; 1/2/3 tonos; stepped levels -36 ... -3 dBFS; bursts 1/10/100ms/1/5/15s; voz cantada seca; plosivas/sibilantes; rap; notas sostenidas.
4. Capturar salida por plugin UAD en render offline (si plugin/host permite) y por hardware con loopback de conversores medido/calibrado y retardo corregido.
5. Ajustar modelo minimizando pérdidas separadas: error de GR vs tiempo/dB, espectro THD/IMD, frecuencia/phase, delta RMS/peak, diferencias de loudness; medir generalización con fixtures **no usados** para fit.
6. Gate auditivo: ABX ciego de vocal solista y de vocal en mezcla con niveles igualados (<0.1 dB como meta), varias personas y reproducibilidad. No confundir "me gusta" con "1:1".
7. Entrega cuando: test del usuario cantando por Apollo y live monitoring sin click, sesión guardada/reabierta, copy/paste, MIDI/VST3 automation sidechain unaffected, Windows Debug+Release, macOS cuando exista.

**DISPONIBILIDAD CONFIRMADA POR EL ARTISTA:** Tiene los LA-2As de Universal Audio. La versión/formato exactos, archivos de prueba renderizados y licencia concreta aún no están identificados. No asumimos acceso remoto a la instalación. Pedir un par dry/wet renderizado de la misma voz cuando el núcleo físico esté listo para calibrar, no pedir que vuelva a decir que tiene UAD.

## VEREDICTOS
- **Alcance del producto:** SOLO UN APEX Opto 2A, UAD Gray como referencia de calibración y otros modelos solo comparadores. CONFIRMADO.
- **Auditoría de topología 1966 y diferencias de versión:** FUENTES ENCONTRADAS.
- **Hardware netlist completo v1 + SPICE simulation:** PENDIENTE de transcripción/validación de esquema y parámetros.
- **T4 física + válvulas + transformadores APEX:** NO implementado.
- **A/B contra UAD Collection/hardware:** NO realizado; necesitamos referencia procesada/mediciones con procedencia.
- **¿Mismo algoritmo interno propietario que UA?** DESCONOCIDO/NO ACCESIBLE. Podemos buscar similitud medible y desarrollar internamente nuestra propia implementación legal e independiente.
- **¿Apto lanzamiento profesional?** NO hasta gates reales; no emplear prototipo actual como compresor de referencia de canción profesional sin evaluación.
