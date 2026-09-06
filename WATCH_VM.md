# Watch VM — BASE standard

Implementation: `os_code/core/rs_vm/`
Standard: https://github.com/sera5m/vulcan-lang/blob/main/STANDARD.md

The watch is a **second interpreter** of the same language, not a consumer of `vulcan_run.py`.

| BASE rule | Watch |
|-----------|--------|
| `.vul` source | `rsvm_compile` / `rsvm_eval` |
| LUT sin/cos/tan degrees + wrap | `RSVM_OP_SIN` … `SIN_AMP`, Q15 table |
| `@latex_internal` | `RSVM_PROP_LATEX_INT` + `rs_vm_latex.c` |
| `@sig_gen_graph_preview` | ignored on device |
| IDE | not linked |

ESP-IDF SRCS are the C VM only.
