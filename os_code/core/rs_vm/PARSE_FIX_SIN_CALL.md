Insert in primary() after ident is parsed, BEFORE slot LOAD:

```
if (peek(p) == '(' &&
    (strcmp(id, "sin") == 0 || strcmp(id, "cos") == 0 ||
     strcmp(id, "tan") == 0 || strcmp(id, "sin_amp") == 0)) {
    getc_(p);
    expr(p);
    if (strcmp(id, "sin_amp") == 0) { expect(p, ","); expr(p); }
    expect(p, ")");
    if (strcmp(id, "sin") == 0) emit_u8(p, RSVM_OP_SIN);
    else if (strcmp(id, "cos") == 0) emit_u8(p, RSVM_OP_COS);
    else if (strcmp(id, "tan") == 0) emit_u8(p, RSVM_OP_TAN);
    else emit_u8(p, RSVM_OP_SIN_AMP);
    return;
}
```

Without this, print(sin(90)) is `LOAD sin` then leftover `(90)` → expected ')'.
Patched copy lives with the desktop tree; apply to rs_vm_parse.cpp on this repo.
