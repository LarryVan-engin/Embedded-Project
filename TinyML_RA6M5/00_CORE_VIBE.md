# Chapter 0 -- The Core Vibe

Three rules. Non-negotiable. Break one and entropy wins.

---

## Vibe Coding Workflow

```
1. Create PREFIX_Name.md in docs/
2. Add [[wikilink]] to one related note
3. Return to your terminal
```

That is the entire process. Everything else in this book is refinement.

---

## Rule 1 -- Never Folder

All notes live **flat** inside `docs/`. No subfolders. No exceptions.

**Why it matters:** Foam resolves `[[wikilinks]]` by filename. The moment you introduce `docs/hardware/mcu/`, every link must encode a relative path. This breaks graph traversal, complicates refactoring, and adds cognitive overhead for zero information gain. Prefix naming (`HW_`, `FW_`, `API_`) achieves the same categorization that folders attempt, without any of the path coupling.

- `docs/hardware/mcu/uart/` -- wrong. Path-coupled, link-hostile.
- `docs/HW_MCU_UART.md` -- correct. Flat, linkable, greppable.

> Flat structure + prefix naming = O(1) navigation.

---

## Rule 2 -- Link or Die

A note without `[[wikilinks]]` is an orphan node. It will never surface in graph traversal, backlink queries, or associative searches. It is dead weight.

**Why it matters:** The value of a Zettelkasten is proportional to **edge density**, not node count. A thousand unlinked notes are worse than fifty well-connected ones. Every link you create is a bidirectional edge -- Foam indexes both directions automatically. Broken links (`[[FW_Not_Written_Yet]]`) are valid placeholders; they signal missing knowledge and will resolve when the target note is created.

- Mention a module, register, API, or concept -- **link it**
- If the target note does not exist yet -- **link it anyway**
- Every note must have at least **one** outgoing `[[wikilink]]`

> A note without links is a note that does not exist.

---

## Rule 3 -- Hardware is Truth

Every firmware note (`FW_`) must reference a hardware note (`HW_`). No driver exists in a vacuum.

**Why it matters:** In embedded systems, the register definition is the contract. Firmware is merely the client that fulfills it. When a `FW_` note lacks a `HW_` parent, you lose traceability between the datasheet constraint and the code that implements it. Bugs become untraceable. Constraints become invisible. The system model fractures.

- `FW_I2C_Driver` must link `[[HW_MCU_I2C]]`
- `RCA_UART_Overrun` must trace to `[[HW_MCU_UART]]` constraint violated
- If no `HW_` note exists for your driver -- **create the HW_ note first**

> Registers are the single source of truth. Firmware is a subordinate.

---

## Atomicity

Before saving any note, apply the deletion test:

*"If I delete this file, do I lose exactly one piece of knowledge?"*

- `HW_MCU_I2C` -- one peripheral. Pass.
- `RCA_SPI_Clock_Polarity` -- one bug. Pass.
- `I2C_Everything` -- hardware + firmware + bugs. Fail. Split it.

---

## Frontmatter

Every note. No exceptions.

```yaml
---
type: <note-type>
status: todo | in-progress | done | blocked | deprecated
last_updated: YYYY-MM-DD
---
```

Tags go on the line immediately after the heading:

```markdown
# FW_UART_Driver

Tags: #done #firmware #uart
```

**Why frontmatter matters:** It enables programmatic filtering. Agents, scripts, and Foam queries can partition the knowledge base by `type` and `status` without parsing free-text content.
