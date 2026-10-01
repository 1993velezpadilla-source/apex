import { el } from "./domCore.js";
import { createTicks } from "./tickCore.js";

export function createFader({ level = 60, master = false }) {
  const core = el("div", "fader-core", {
    style: { "--fader-level": `${level}%` }
  });

  const rail = el("div", "fader-rail");
  rail.appendChild(el("div", "fader-fill"));
  rail.appendChild(el("div", "fader-cap", {
    role: "slider",
    "aria-label": master ? "Master fader" : "Channel fader"
  }));

  core.append(createTicks(), rail);
  return core;
}
