import { el } from "./domCore.js";

export function createKnob({ label = "PAN", rotation = 0 }) {
  const core = el("div", "knob-core");
  const knob = el("div", "knob", {
    style: { "--knob-rotation": `${rotation}deg` },
    role: "slider",
    "aria-label": label
  });
  const text = el("div", "knob-label", { text: label });

  core.append(knob, text);
  return core;
}
