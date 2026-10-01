import { el } from "./domCore.js";

export function createHeaderButtons(channel) {
  const row = el("div", "button-row");

  const buttons = [
    { key: "mute", label: "M" },
    { key: "solo", label: "S" }
  ];

  buttons.forEach((button) => {
    row.appendChild(
      el("div", `channel-button${channel[button.key] ? " is-lit" : ""}`, {
        text: button.label,
        title: button.key
      })
    );
  });

  return row;
}
