# Chapter 1 -- Embedded Systems

**Scope:** Bare-metal firmware, RTOS, driver development, PCB co-documentation, hardware-software co-design.

**Prerequisite:** [Chapter 0 -- The Core Vibe](00_CORE_VIBE.md).

---

## Anti-Patterns

Stop doing these. Each one actively degrades your knowledge base.

- **One monolithic DOCUMENTATION.md** -- cannot search, link, or navigate. Split into atomic notes by entity.
- **Deep folders** `docs/hw/mcu/periph/uart/` -- breaks wikilink resolution. Use flat `HW_MCU_UART.md`.
- **Copy-pasting entire datasheet sections** -- creates bloated, unreadable notes. Summarize the constraint, reference the section number.
- **FW_ notes without HW_ links** -- firmware without hardware traceability is firmware without a contract. Every driver has a register parent.
- **No links between notes** -- an unlinked note is invisible to graph traversal. Link or delete.
- **Long narrative paragraphs** -- neither agents nor humans parse prose efficiently. Use headings, tables, code blocks.
- **Deleting outdated notes** -- mark `status: deprecated`. History has value for root cause analysis.

---

## Vibe Coding Workflow

```
1. Reading datasheet   -> create HW_MCU_Peripheral.md
2. Writing driver      -> create FW_Module.md -> link [[HW_]]
3. Hit a bug           -> create RCA_Bug.md -> link [[FW_]] + [[HW_]]
4. Update INDEX.md
```

Do this inline, while your build is compiling. It takes 30 seconds.

---

## The Hardware-Firmware Contract

This is the architectural principle that distinguishes embedded Zettelkasten from generic note-taking.

**Hardware defines the contract. Firmware implements it.**

The register map is the interface specification. The datasheet constraint is the precondition. The driver code is the implementation. Every `FW_` note is a client of at least one `HW_` note. Every `RCA_` note traces to a `HW_` constraint that was violated or misunderstood.

```
HW_MCU_I2C  <--------- Datasheet Section 12.3
    |
    | [[constraint]]
    v
FW_I2C_Driver <-------- firmware/i2c_driver.c
    |
    | [[bug]]
    v
RCA_I2C_ACK_Failure --> traces back to HW_ timing constraint
    |
    | [[fix applied]]
    v
FW_I2C_Driver (updated) + ARCH_I2C_Retry_Strategy
```

**Why this matters:** When a bug surfaces 6 months from now, the RCA note links directly to the hardware constraint that was violated. Without this chain, debugging starts from zero every time. With it, root cause identification is a graph traversal.

---

## Project Layout

```
PROJECT_ROOT/
  firmware/              <- C/C++ source
  hardware/              <- Schematics, datasheets (reference only)
  docs/                  <- Zettelkasten (flat, no subfolders)
    INDEX.md
    HW_*.md   FW_*.md   RCA_*.md   PCB_*.md
    ARCH_*.md TOOL_*.md  LOG_*.md   REF_*.md   TEST_*.md
    _templates/
  tests/
```

All Zettelkasten notes live flat inside `docs/`. The `firmware/` and `hardware/` directories hold source code and reference files, not knowledge base notes.

---

## Foam Graph Colors

```jsonc
{
  "foam.graph.style": {
    "node": {
      "HW_*":   { "color": "#e74c3c" },
      "FW_*":   { "color": "#2ecc71" },
      "RCA_*":  { "color": "#f39c12" },
      "ARCH_*": { "color": "#9b59b6" },
      "INDEX":  { "color": "#3498db" }
    }
  }
}
```

Color encodes layer. Red nodes (hardware) should always have green children (firmware). Orange nodes (bugs) should always have edges to both.

---

## Note Types

- **`HW_`** -- Hardware. Peripheral, register map, pin config, electrical constraint. One note per peripheral or functional block. Example: `HW_MCU_I2C.md`
- **`FW_`** -- Firmware. Driver module, API surface, init sequence, ISR logic. Must link to its `HW_` parent. Example: `FW_I2C_Driver.md`
- **`RCA_`** -- Root Cause Analysis. One bug, one note. Links to the `FW_` module affected and the `HW_` constraint violated. Example: `RCA_I2C_ACK_Failure.md`
- **`PCB_`** -- PCB/Schematic. Board layout, net, power rail, component placement. Example: `PCB_PowerSupply_3V3.md`
- **`ARCH_`** -- Architecture Decision. Design choice with rationale and trade-offs. Example: `ARCH_RTOS_vs_BareMetal.md`
- **`TOOL_`** -- Toolchain. Compiler, debugger, build system, flash tool. Example: `TOOL_CMake_Setup.md`
- **`LOG_`** -- Debug Log. Session notes, scope captures, measurements. Example: `LOG_2026-04-22_UART_Noise.md`
- **`REF_`** -- Reference. Datasheet section summary, application note distillation. Example: `REF_AN_I2C_PullUp_Calc.md`
- **`TEST_`** -- Test. Test plan, test suite, coverage notes. Example: `TEST_I2C_Integration.md`

---

## Templates

### HW_ -- Hardware Register / Peripheral

```markdown
# HW_${MCU}_${Peripheral}

Tags: #todo #hardware

${One-line description}. Driver: [[FW_${Driver}]].

---

## Peripheral Overview

Base address: `0x________`. Channels: ___.
Clock gate: [[FW_Clock_Driver]] -- MSTPCR_ bit __.

## Register Map

| Offset | Register | Function |
|--------|----------|----------|
| 0x00   | REG      | ...      |

## Critical Constraints

### ${ConstraintName}
${Datasheet section ref + what the silicon requires}

## Pin Configuration

| Channel | Pin_A | Pin_B | PSEL |
|---------|-------|-------|------|

## References

${MCU} Hardware Manual, Section __.
```

**Why register maps in notes:** The register table is the API contract between hardware and firmware. Capturing it here means the firmware developer does not need to context-switch to a 2000-page PDF. The note is the cache.

---

### FW_ -- Firmware Module

```markdown
# FW_${ModuleName}

Tags: #todo #firmware

${One-line description}. Files: `${path}`.
Hardware: [[HW_${Peripheral}]]. Bugs: [[RCA_...]].

---

## API

```c
void Module_Init(...);
```

## Init Sequence

${Steps with code + HW register references}

## Key Implementation Details

${ISR handling, register bit manipulation, timing-critical sequences}
```

**Why `Hardware:` is mandatory:** Without it, the firmware note is decoupled from its contract. You cannot verify correctness against the register specification if you cannot find it.

---

### RCA_ -- Root Cause Analysis

```markdown
# RCA_${BugName}

Tags: #todo #bug #${severity}

${Problem statement}. Module: [[FW_...]]. Hardware: [[HW_...]].

---

## Symptoms
- ${Observable behavior}

## Hardware Root Cause
${Which constraint was violated, with datasheet reference}

## Code (Wrong)
```c
/* original */
```

## Code (Fixed)
```c
/* corrected */
```

## Verification
${How confirmed -- test, scope, logic analyzer}

## Lessons Learned
- ${Pattern to remember}
```

---

### ARCH_ -- Architecture Decision

```markdown
# ARCH_${Decision}

Tags: #todo #architecture

**Status:** proposed | accepted | deprecated
**Date:** YYYY-MM-DD
**Context:** [[FW_...]], [[HW_...]]

---

## Problem
${Why this decision is needed}

## Options
- **A:** ${pros / cons}
- **B:** ${pros / cons}

## Decision
${Chosen option + rationale}

## Consequences
${Impact and trade-offs accepted}
```

---

## Daily Workflows

### New Module

```
1. Datasheet section   -> HW_ note (register map, constraints)
2. Write driver        -> FW_ note (API, init, link to HW_)
3. Bug encountered     -> RCA_ note (link to FW_ + HW_)
4. Update INDEX.md
5. #todo -> #done
```

### Debug Session

```
1. LOG_ note           -> symptoms, observations, scope captures
2. Root cause found    -> RCA_ note (link HW_ constraint violated)
3. Fix applied         -> update FW_ note
4. Update INDEX.md
```

### Refactoring

```
1. Foam Graph          -> identify orphan and oversized nodes
2. Orphans             -> add links or remove
3. Oversized notes     -> split by atomicity test
4. Update status tags
```

---

## Wikilink Discipline

Link whenever you reference an entity that has or should have a note:

```markdown
Hardware constraints: [[HW_MCU_I2C]]. Bug fix: [[RCA_I2C_ACK_Failure]].
```

Not:

```markdown
Hardware constraints documented elsewhere. Bug fixed previously.
```

The second form provides zero navigability. The first provides bidirectional traversal.

---

## INDEX.md Template

```markdown
---
type: hub
status: done
last_updated: YYYY-MM-DD
---

# ${PROJECT} -- Knowledge Base

Tags: #done #system

> Target: ${MCU} | Board: ${board} | Toolchain: ${toolchain}

## Hardware Layer
| Note | Covers |
|------|--------|
| [[HW_...]] | ... |

## Firmware Layer
| Note | Files |
|------|-------|
| [[FW_...]] | `path/` |

## Root Cause Analysis
| Note | Bug |
|------|-----|
| [[RCA_...]] | ... |

## Architecture
| Note | Decision |
|------|----------|
| [[ARCH_...]] | ... |
```

When a layer exceeds 15 notes, create a sub-index (`INDEX_Hardware.md`). Sub-indexes always link back to `[[INDEX]]`.

---

## Checklist

- [ ] Correct prefix?
- [ ] Frontmatter present (`type`, `status`, `last_updated`)?
- [ ] Tags on first line after heading?
- [ ] Atomic -- exactly one entity?
- [ ] `[[wikilinks]]` to related notes?
- [ ] `FW_` links to `HW_`?
- [ ] INDEX.md updated?
- [ ] Code blocks for register values, API signatures, sequences?
- [ ] Datasheet section referenced?
