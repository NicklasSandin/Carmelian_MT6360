# DTB graft: mainline kernel on the stock (LK-accepted) DTB

The stock camellian DTB is the only boot-DTB blob MTK LK accepts and patches.
The mainline kernel needs A22X-style device nodes. The graft = stock DTB
base + verbatim A22X node copies (their phandle references resolve to the
same values in the stock tree) + a compatible patch on the watchdog node.

- `graftr1.dts` — iteration 1 (t-phy + MUSB + watchdog compatible):
  produced the first full first light on the mainline kernel
- `mainline-first-light-console.log` — the 305-second console
  (boot → SSH → five-minute ceiling → clean reboot)
- `first-mainline-console.log` — the earlier stock-DTB-only console
  (kernel alive but no USB nodes, 28 s WDT death)

Iteration 2 (planned): mainline-shaped mt6360_pmu_dts for the microSD
power (LDO3/LDO5) and the msdc1 regulator wiring.
