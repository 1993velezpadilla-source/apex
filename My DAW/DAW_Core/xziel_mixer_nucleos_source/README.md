# XZIEL Haute Glass Mixer — source modular por núcleos

Proyecto vanilla HTML/CSS/JS separado en núcleos para tocar cualquier pieza del visual sin romper las demás.

## Cómo abrir

Abre `index.html` en el navegador.

## Núcleos CSS

- `00_tokens.css`: variables globales: colores, blur, radios, tamaños.
- `01_stage.css`: fondo dusk/studio, vapor lights y contenedor.
- `02_pane.css`: cristal laminado de cada strip.
- `03_header_buttons.css`: botones M/S.
- `04_knob.css`: knob, bezel multi-tono y pointer pearl-white.
- `05_fader.css`: rail, fill y fader cap chrome/gold.
- `06_decibel_ticks.css`: marcas dB y labels.
- `07_master.css`: pane master, blur 32px, filamentos gold, capline, knob y fader especiales.
- `08_responsive.css`: ajustes mobile.

## Núcleos JS

- `main.js`: data de channels.
- `mixerCore.js`: monta el mixer completo.
- `stripCore.js`: arma cada channel strip.
- `buttonCore.js`: botones M/S.
- `knobCore.js`: knob independiente.
- `faderCore.js`: fader independiente.
- `tickCore.js`: ticks dB independientes.
- `domCore.js`: helper pequeño para crear elementos.

## Cambios rápidos

- Blur normal: `--pane-blur` en `00_tokens.css`.
- Blur master: `--master-blur` en `00_tokens.css`.
- Tamaño del fader cap: `.fader-cap` en `05_fader.css`.
- Ticks dB: `DECIBEL_TICKS` en `js/cores/tickCore.js`.
- Channels/nombres/niveles: array `channels` en `js/main.js`.
