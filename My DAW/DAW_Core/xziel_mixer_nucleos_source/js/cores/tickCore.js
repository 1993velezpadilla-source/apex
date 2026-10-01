import { el } from "./domCore.js";

const DECIBEL_TICKS = [
  { label: "+6", y: 7 },
  { label: "0", y: 25 },
  { label: "-6", y: 43 },
  { label: "-12", y: 61 },
  { label: "-∞", y: 86 }
];

export function createTicks() {
  const ticks = el("div", "ticks");

  DECIBEL_TICKS.forEach((tick) => {
    const row = el("div", "tick", { style: { "--tick-y": `${tick.y}%` } });
    row.appendChild(el("span", "tick-label", { text: tick.label }));
    ticks.appendChild(row);
  });

  return ticks;
}

export { DECIBEL_TICKS };
