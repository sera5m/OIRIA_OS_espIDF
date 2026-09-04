# Vulcan IDE (EndeavourOS / Wayland)

Small GTK4 editor that talks to the watch over the **same USB-C serial
`idf.py monitor` uses**. On ESP32-S3 that is usually USB-Serial-JTAG
(`/dev/ttyACM0`, 115200).

It does **not** replace the chip-to-chip RSDOM link. This is PC → watch.

## Install (EndeavourOS)

```bash
sudo pacman -S python-gobject gtk4 python-pyserial
# optional nicer fonts / portal
sudo pacman -S xdg-desktop-portal-gtk
```

Plug the watch in **before** opening the port. Add yourself to `uucp` so
you do not need root:

```bash
sudo usermod -aG uucp "$USER"
# log out / in
```

Run:

```bash
chmod +x vulcan-ide vulcan_ide.py
./vulcan-ide
# or
python3 vulcan_ide.py examples/basic.vul
```

## Target / files / tyrant

| Control | Effect |
|--------|--------|
| Target dropdown | **This PC** — run buffer with `rsvm`/`vulcan` if on PATH. **Watch** — framed USB upload |
| File list | `.vul` / `.bvul` in the current folder (Folder / Refresh / New) |
| Tyrant mode (PC) | PC is puppeteer: exec on watch, `TY PING`, GPIO/IO read+write |
| Run | Watch or tyrant → upload; This PC and tyrant off → local runner |
| Ports / Connect | `/dev/ttyACM*` or `/dev/ttyUSB*` @ 115200 |

Tyrant replies: `TY PONG`, `TY IO pin=N val=V`, `TY ERR …`

Watch replies (also visible in `idf.py monitor`):

```
VULCAN READY
VULCAN OK steps=29
VULCAN ERR L4:1 expected ';'
```

## Firmware side

Flash the tree that starts `appManager::start_serial_terminal()` and
understands the `<<VUL` / `VUL>>` frame. Console config already has
`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`.

If Connect works but Send does nothing, you are on the wrong tty
(JTAG vs CP210x). Hit **Ports** after plugging in and try the other one.

## Not in this app

- Local JIT / gcc AOT
- Wayland window compositing of the watch UI
- RSDOM tyrant↔puppet (that is chip-to-chip UART GPIO7/8)
