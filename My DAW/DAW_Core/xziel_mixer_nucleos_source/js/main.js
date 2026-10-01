import { createMixer } from "./cores/mixerCore.js";

const channels = [
  { id: "s1", title: "Track 1", pan: -35, level: 63, solo: true },
  { id: "s2", title: "Track 2", pan: 12, level: 47 },
  { id: "s3", title: "Vox", pan: -8, level: 70, mute: true },
  { id: "s4", title: "Keys", pan: 28, level: 55 },
  { id: "s5", title: "FX", pan: 42, level: 38 },
  { id: "s6", title: "Drums", pan: -18, level: 76 },
  { id: "master", title: "MASTER", pan: 0, level: 68, master: true }
];

createMixer(document.getElementById("app"), channels);
