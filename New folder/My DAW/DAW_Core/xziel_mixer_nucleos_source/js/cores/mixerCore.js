import { el } from "./domCore.js";
import { createStrip } from "./stripCore.js";

export function createMixer(root, channels) {
  root.innerHTML = "";
  const layout = el("section", "mixer-layout");

  channels.forEach((channel) => {
    layout.appendChild(createStrip(channel));
  });

  root.appendChild(layout);
}
