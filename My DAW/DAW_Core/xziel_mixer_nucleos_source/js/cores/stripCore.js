import { el } from "./domCore.js";
import { createHeaderButtons } from "./buttonCore.js";
import { createKnob } from "./knobCore.js";
import { createFader } from "./faderCore.js";

export function createStrip(channel) {
  const strip = el("article", `strip${channel.master ? " master" : ""}`, {
    id: channel.id,
    "data-strip": channel.id
  });

  const aura = el("div", "strip-aura");
  const pane = el("div", "pane");

  if (channel.master) pane.appendChild(el("div", "master-capline"));
  pane.appendChild(el("div", "strip-title", { text: channel.title }));
  pane.appendChild(createHeaderButtons(channel));
  pane.appendChild(createKnob({ label: channel.master ? "HALO" : "PAN", rotation: channel.pan }));
  pane.appendChild(createFader({ level: channel.level, master: channel.master }));

  strip.append(aura, pane);
  return strip;
}
