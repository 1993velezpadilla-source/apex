# DAW Brain OpenCode Agent — Installation

1. Copy your canonical Brain into the root of the APEX repository and name it:

   `DAW_BRAIN.md`

2. Copy the included `.opencode` directory and `AGENTS.md` into the same repository root.

3. Merge `opencode.json` with any existing OpenCode configuration. Do not overwrite provider or model settings you already use.

4. Open a terminal in the APEX repository and start OpenCode:

   `opencode`

5. The configured default primary agent is `daw-brain`. If another agent is active, press `Tab` until `daw-brain` appears.

6. Talk to it normally, for example:

   `Explícame cómo APEX debe manejar un cambio dinámico de latencia de un plugin.`

   `Audita el live monitoring actual contra el Brain. No cambies código todavía.`

   `Encuentra por qué este export incluye audio live input y dame evidencia de archivo y línea.`

7. The agent uses whichever LLM provider/model OpenCode has connected. The Brain is not a new neural model; it is the agent's canonical knowledge and operating contract.

## Optional model pinning

To force one model, add this line to the frontmatter of `.opencode/agents/daw-brain.md`:

`model: provider/model-name`

Leaving it out lets the agent use the currently selected/default OpenCode model.

## Important

Do not place all 1.9 MB of the Brain directly inside the agent prompt. Keep it as `DAW_BRAIN.md` and let the agent read relevant sections on demand. This reduces context waste and makes repository evidence easier to combine with the Brain.
