# Module graph

```
vulcan-lang                 BASE + desktop VM
  https://github.com/sera5m/vulcan-lang

     +-- vulcan-ide         https://github.com/sera5m/vulcan-ide
     |     BASE + desktop VM
     |     NOT a firmware dependency
     |
     `-- watch VM           THIS REPO  os_code/core/rs_vm
           BASE only
           C interpreter, same standard
```
