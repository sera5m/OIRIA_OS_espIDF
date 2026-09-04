#!/usr/bin/env python3
"""Vulcan IDE — Wayland editor, file tree, PC/watch target, PC tyrant I/O.

USB-C console (idf.py monitor style), usually /dev/ttyACM0 @ 115200.

Script upload:
    <<VUL
    <source>
    VUL>>

Tyrant lines (PC orders the watch):
    TY PING
    TY GPIO W <pin> <0|1>
    TY GPIO R <pin>
    TY IO W <pin> <val>
    TY IO R <pin>
"""
from __future__ import annotations

import os
import sys
import glob
import shutil
import threading
import subprocess
from pathlib import Path

os.environ.setdefault("GDK_BACKEND", "wayland")

try:
    import gi
    gi.require_version("Gtk", "4.0")
    gi.require_version("Gdk", "4.0")
    from gi.repository import Gtk, Gdk, GLib, Gio
except Exception as e:
    sys.stderr.write(
        "Need GTK4 + PyGObject.\n"
        "  sudo pacman -S python-gobject gtk4 python-pyserial\n"
        f"{e}\n"
    )
    sys.exit(1)

try:
    import serial
    from serial.tools import list_ports
except Exception:
    serial = None
    list_ports = None

APP_ID = "dev.oiria.VulcanIde"
HERE = Path(__file__).resolve().parent
EXAMPLES = HERE / "examples"
VUL_EXTS = {".vul", ".bvul"}
DEFAULT_SRC = """set_step_depth 20000;

fn main in[] out[] {
  print("hello from vulcan-ide");
  i32 n = 3;
  do n[n] {
    print(n);
  }
  return;
}
"""


def discover_ports():
    found = []
    if list_ports:
        for p in list_ports.comports():
            found.append(p.device)
    if not found:
        found = sorted(
            glob.glob("/dev/ttyACM*")
            + glob.glob("/dev/ttyUSB*")
            + glob.glob("/dev/ttyS*")
        )
    return found or ["/dev/ttyACM0"]


def list_project_files(folder: Path):
    if not folder.is_dir():
        return []
    out = []
    for p in sorted(folder.iterdir(), key=lambda x: (x.is_dir(), x.name.lower())):
        if p.name.startswith("."):
            continue
        if p.is_file() and p.suffix.lower() in VUL_EXTS:
            out.append(p)
        elif p.is_file() and p.suffix.lower() in {".md", ".txt"}:
            out.append(p)
    return out


class SerialLink:
    def __init__(self, on_rx):
        self.on_rx = on_rx
        self.ser = None
        self._stop = threading.Event()
        self._th = None
        self.lock = threading.Lock()

    @property
    def open(self):
        return self.ser is not None and self.ser.is_open

    def connect(self, port, baud):
        if serial is None:
            raise RuntimeError("python-pyserial is not installed")
        self.close()
        self.ser = serial.Serial(port, baudrate=int(baud), timeout=0.05)
        self._stop.clear()
        self._th = threading.Thread(target=self._rx_loop, daemon=True)
        self._th.start()

    def close(self):
        self._stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def write(self, data: bytes):
        with self.lock:
            if not self.open:
                raise RuntimeError("serial not connected — Connect first")
            self.ser.write(data)
            self.ser.flush()

    def _rx_loop(self):
        buf = b""
        while not self._stop.is_set() and self.ser:
            try:
                chunk = self.ser.read(256)
            except Exception as e:
                GLib.idle_add(self.on_rx, f"[serial] {e}\n")
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.replace(b"\r", b"").decode("utf-8", "replace") + "\n"
                GLib.idle_add(self.on_rx, text)


class VulcanIde(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_OPEN)
        self.win = None
        self.editor = None
        self.log = None
        self.port_drop = None
        self.baud = None
        self.status = None
        self.target_drop = None
        self.tyrant_on = None
        self.file_list = None
        self.dir_label = None
        self.pin_entry = None
        self.val_entry = None
        self.path = None
        self.workdir = EXAMPLES if EXAMPLES.is_dir() else Path.cwd()
        self.link = SerialLink(self._append_log)
        self.connect("shutdown", lambda *_: self.link.close())

    def do_activate(self):
        if self.win:
            self.win.present()
            return
        self.win = Gtk.ApplicationWindow(application=self, title="Vulcan IDE")
        self.win.set_default_size(1100, 760)
        self.win.set_child(self._build())
        self.tyrant_box.set_sensitive(False)
        self.win.present()
        self._refresh_files()
        self._set_text(DEFAULT_SRC)
        self._append_log("Target: This PC runs locally if `rsvm` is on PATH.\n")
        self._append_log("Target: Watch uploads <<VUL>> over USB-C serial.\n")
        self._append_log("Tyrant mode: PC orders the watch (exec + GPIO/IO R/W).\n")

    def do_open(self, files, n_files, hint):
        self.do_activate()
        if n_files:
            p = Path(files[0].get_path())
            if p.is_dir():
                self.workdir = p
                self._refresh_files()
            else:
                self.workdir = p.parent
                self._refresh_files()
                self._open_path(str(p))

    def _build(self):
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        hb = Gtk.HeaderBar()
        self.win.set_titlebar(hb)
        for label, cb in (("Open", self._on_open), ("Save", self._on_save), ("New", self._on_new)):
            b = Gtk.Button(label=label)
            b.connect("clicked", cb)
            hb.pack_start(b)
        self.target_drop = Gtk.DropDown.new_from_strings(["This PC", "Watch"])
        self.target_drop.set_selected(1)
        self.target_drop.connect("notify::selected", self._on_target_changed)
        hb.pack_start(Gtk.Label(label="  Target"))
        hb.pack_start(self.target_drop)
        self.port_drop = Gtk.DropDown.new_from_strings(discover_ports())
        refresh = Gtk.Button(label="Ports")
        refresh.connect("clicked", self._refresh_ports)
        self.baud = Gtk.Entry(text="115200", width_chars=7)
        conn = Gtk.Button(label="Connect")
        conn.add_css_class("suggested-action")
        conn.connect("clicked", self._on_connect)
        disc = Gtk.Button(label="Hang up")
        disc.connect("clicked", lambda *_: self._hangup())
        hb.pack_end(disc)
        hb.pack_end(conn)
        hb.pack_end(self.baud)
        hb.pack_end(self.port_drop)
        hb.pack_end(refresh)
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        toolbar.add_css_class("toolbar")
        toolbar.set_margin_start(8)
        toolbar.set_margin_end(8)
        toolbar.set_margin_top(6)
        toolbar.set_margin_bottom(6)
        run = Gtk.Button(label="Run")
        run.add_css_class("suggested-action")
        run.connect("clicked", self._on_run)
        ping = Gtk.Button(label="Ping")
        ping.connect("clicked", self._on_ping)
        self.tyrant_on = Gtk.CheckButton(label="Tyrant mode (PC)")
        self.tyrant_on.connect("toggled", self._on_tyrant_toggled)
        self.status = Gtk.Label(label="disconnected · watch", xalign=0)
        self.status.set_hexpand(True)
        toolbar.append(run)
        toolbar.append(ping)
        toolbar.append(self.tyrant_on)
        toolbar.append(self.status)
        root.append(toolbar)
        body = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        body.set_vexpand(True)
        body.set_wide_handle(True)
        body.set_start_child(self._build_files())
        body.set_resize_start_child(False)
        body.set_shrink_start_child(False)
        right = Gtk.Paned(orientation=Gtk.Orientation.VERTICAL)
        right.set_wide_handle(True)
        right.set_vexpand(True)
        mid = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        mid.set_wide_handle(True)
        self.editor = Gtk.TextView(monospace=True)
        self.editor.set_wrap_mode(Gtk.WrapMode.NONE)
        self.editor.set_top_margin(8)
        self.editor.set_left_margin(8)
        sc1 = Gtk.ScrolledWindow()
        sc1.set_child(self.editor)
        sc1.set_vexpand(True)
        sc1.set_hexpand(True)
        mid.set_start_child(sc1)
        self.tyrant_box = self._build_tyrant()
        mid.set_end_child(self.tyrant_box)
        mid.set_resize_end_child(False)
        mid.set_position(700)
        right.set_start_child(mid)
        self.log = Gtk.TextView(editable=False, monospace=True)
        self.log.set_wrap_mode(Gtk.WrapMode.CHAR)
        sc2 = Gtk.ScrolledWindow()
        sc2.set_child(self.log)
        sc2.set_min_content_height(160)
        right.set_end_child(sc2)
        right.set_position(520)
        body.set_end_child(right)
        body.set_position(200)
        root.append(body)
        return root

    def _build_files(self):
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        col.set_margin_start(6)
        col.set_margin_end(4)
        col.set_margin_top(6)
        col.set_size_request(190, -1)
        self.dir_label = Gtk.Label(xalign=0, wrap=True)
        self.dir_label.add_css_class("dim-label")
        row = Gtk.Box(spacing=4)
        folder = Gtk.Button(label="Folder")
        folder.connect("clicked", self._pick_folder)
        refill = Gtk.Button(label="Refresh")
        refill.connect("clicked", lambda *_: self._refresh_files())
        row.append(folder)
        row.append(refill)
        col.append(row)
        col.append(self.dir_label)
        self.file_list = Gtk.ListBox()
        self.file_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.file_list.connect("row-activated", self._on_file_activated)
        sc = Gtk.ScrolledWindow()
        sc.set_child(self.file_list)
        sc.set_vexpand(True)
        sc.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        col.append(sc)
        return col

    def _build_tyrant(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.set_margin_start(8)
        box.set_margin_end(8)
        box.set_margin_top(8)
        box.set_size_request(230, -1)
        title = Gtk.Label(label="Tyrant · this PC", xalign=0)
        title.add_css_class("title-4")
        hint = Gtk.Label(label="PC is puppeteer. Watch obeys TY lines and framed exec.", wrap=True, xalign=0)
        hint.add_css_class("dim-label")
        box.append(title)
        box.append(hint)
        exec_btn = Gtk.Button(label="Order: exec buffer on watch")
        exec_btn.connect("clicked", lambda *_: self._send_watch(self._get_text()))
        box.append(exec_btn)
        ping = Gtk.Button(label="Order: TY PING")
        ping.connect("clicked", lambda *_: self._ty_send("TY PING\n"))
        box.append(ping)
        grid = Gtk.Grid(column_spacing=6, row_spacing=6)
        grid.attach(Gtk.Label(label="pin", xalign=0), 0, 0, 1, 1)
        self.pin_entry = Gtk.Entry(text="7", width_chars=4)
        grid.attach(self.pin_entry, 1, 0, 1, 1)
        grid.attach(Gtk.Label(label="val", xalign=0), 0, 1, 1, 1)
        self.val_entry = Gtk.Entry(text="1", width_chars=4)
        grid.attach(self.val_entry, 1, 1, 1, 1)
        box.append(grid)
        rw = Gtk.Box(spacing=6)
        wr = Gtk.Button(label="IO write")
        wr.connect("clicked", self._on_io_write)
        rd = Gtk.Button(label="IO read")
        rd.connect("clicked", self._on_io_read)
        rw.append(wr)
        rw.append(rd)
        box.append(rw)
        raw_l = Gtk.Label(label="raw TY / console line", xalign=0)
        self.raw_entry = Gtk.Entry(placeholder_text="TY GPIO R 8")
        self.raw_entry.connect("activate", self._on_raw)
        send_raw = Gtk.Button(label="Send line")
        send_raw.connect("clicked", self._on_raw)
        box.append(raw_l)
        box.append(self.raw_entry)
        box.append(send_raw)
        return box

    def _target(self):
        return "pc" if self.target_drop.get_selected() == 0 else "watch"

    def _on_target_changed(self, *_):
        self._touch_status()
        t = self._target()
        self._append_log("[target] This PC\n" if t == "pc" else "[target] Watch\n")

    def _on_tyrant_toggled(self, btn):
        on = btn.get_active()
        self.tyrant_box.set_sensitive(on)
        self._append_log("[tyrant] ON\n" if on else "[tyrant] off\n")
        self._touch_status()

    def _touch_status(self):
        bits = ["connected" if self.link.open else "disconnected"]
        bits.append("pc" if self._target() == "pc" else "watch")
        if self.tyrant_on and self.tyrant_on.get_active():
            bits.append("tyrant")
        if self.path:
            bits.append(Path(self.path).name)
        self.status.set_text(" · ".join(bits))

    def _refresh_files(self):
        self.dir_label.set_text(str(self.workdir))
        while True:
            row = self.file_list.get_row_at_index(0)
            if not row:
                break
            self.file_list.remove(row)
        files = list_project_files(self.workdir)
        if not files:
            lab = Gtk.Label(label="(no .vul here)", xalign=0)
            lab.add_css_class("dim-label")
            self.file_list.append(lab)
            return
        for p in files:
            lab = Gtk.Label(label=p.name, xalign=0)
            lab.set_tooltip_text(str(p))
            row = Gtk.ListBoxRow()
            row.set_child(lab)
            row._vul_path = str(p)
            self.file_list.append(row)

    def _on_file_activated(self, _lb, row):
        path = getattr(row, "_vul_path", None)
        if path:
            self._stash_if_named()
            self._open_path(path)

    def _stash_if_named(self):
        if self.path:
            try:
                Path(self.path).write_text(self._get_text(), encoding="utf-8")
            except OSError as e:
                self._append_log("[autosave] %s\n" % e)

    def _pick_folder(self, *_):
        dlg = Gtk.FileDialog(title="Project folder")
        dlg.select_folder(self.win, None, self._folder_done)

    def _folder_done(self, dlg, res):
        try:
            f = dlg.select_folder_finish(res)
        except Exception:
            return
        self.workdir = Path(f.get_path())
        self._refresh_files()

    def _buf(self):
        return self.editor.get_buffer()

    def _get_text(self):
        b = self._buf()
        return b.get_text(b.get_start_iter(), b.get_end_iter(), False)

    def _set_text(self, s):
        self._buf().set_text(s)

    def _append_log(self, s):
        if self.log is None:
            return False
        b = self.log.get_buffer()
        b.insert(b.get_end_iter(), s)
        mark = b.create_mark(None, b.get_end_iter(), False)
        self.log.scroll_to_mark(mark, 0.0, False, 0, 1)
        return False

    def _refresh_ports(self, *_):
        ports = discover_ports()
        self.port_drop.set_model(Gtk.StringList.new(ports))
        self._append_log("ports: " + ", ".join(ports) + "\n")

    def _selected_port(self):
        item = self.port_drop.get_selected_item()
        if item is None:
            return "/dev/ttyACM0"
        return item.get_string()

    def _on_connect(self, *_):
        port = self._selected_port()
        baud = self.baud.get_text().strip() or "115200"
        try:
            self.link.connect(port, baud)
        except Exception as e:
            self.status.set_text("fail: %s" % e)
            self._append_log("[connect] %s\n" % e)
            return
        self._touch_status()
        self._append_log("[connect] %s %s\n" % (port, baud))
        try:
            self.link.write(b"\nTY PING\n")
        except Exception:
            pass

    def _hangup(self):
        self.link.close()
        self._touch_status()
        self._append_log("[hangup]\n")

    def _on_run(self, *_):
        src = self._get_text()
        if not src.strip():
            return
        if self.tyrant_on.get_active() or self._target() == "watch":
            self._send_watch(src)
            return
        self._run_local(src)

    def _run_local(self, src):
        runner = shutil.which("rsvm") or shutil.which("vulcan")
        if not runner:
            self._append_log("[pc] no local rsvm/vulcan on PATH. Use Watch or Tyrant.\n")
            self.status.set_text("no local rsvm — use Watch / Tyrant")
            return
        tmp = Path("/tmp/vulcan_ide_run.vul")
        tmp.write_text(src, encoding="utf-8")
        self._append_log("[pc] %s %s\n" % (runner, tmp))
        def work():
            try:
                p = subprocess.run([runner, str(tmp)], capture_output=True, text=True, timeout=20)
                out = (p.stdout or "") + (p.stderr or "")
                GLib.idle_add(self._append_log, out if out.endswith("\n") else out + "\n")
                GLib.idle_add(self.status.set_text, "pc exit %s" % p.returncode)
            except Exception as e:
                GLib.idle_add(self._append_log, "[pc] %s\n" % e)
        threading.Thread(target=work, daemon=True).start()

    def _send_watch(self, src):
        if not src or not src.strip():
            return
        payload = "clear\n<<VUL\n" + src.replace("\r\n", "\n").rstrip() + "\nVUL>>\n"
        try:
            self.link.write(payload.encode("utf-8"))
        except Exception as e:
            self._append_log("[watch] %s\n" % e)
            self.status.set_text(str(e))
            return
        self.status.set_text("sent to watch — wait VULCAN OK / ERR")
        self._append_log("[watch] framed upload\n")

    def _ty_send(self, line):
        if not line.endswith("\n"):
            line += "\n"
        try:
            self.link.write(line.encode("utf-8"))
        except Exception as e:
            self._append_log("[ty] %s\n" % e)
            self.status.set_text(str(e))
            return
        self._append_log("> " + line)

    def _pin_val(self):
        try:
            pin = int(self.pin_entry.get_text().strip())
        except ValueError:
            pin = 0
        try:
            val = int(self.val_entry.get_text().strip())
        except ValueError:
            val = 0
        return pin, val

    def _on_io_write(self, *_):
        pin, val = self._pin_val()
        self._ty_send("TY IO W %d %d\n" % (pin, val))

    def _on_io_read(self, *_):
        pin, _ = self._pin_val()
        self._ty_send("TY IO R %d\n" % pin)

    def _on_raw(self, *_):
        line = self.raw_entry.get_text().strip()
        if line:
            self._ty_send(line + "\n")

    def _on_ping(self, *_):
        if self.tyrant_on.get_active() or self._target() == "watch":
            self._ty_send("TY PING\n")
        else:
            self._append_log("[pc] pong (local target, no device)\n")

    def _on_open(self, *_):
        dlg = Gtk.FileDialog(title="Open")
        filt = Gtk.FileFilter()
        filt.add_pattern("*.vul")
        filt.add_pattern("*.bvul")
        filt.add_pattern("*")
        dlg.set_default_filter(filt)
        dlg.open(self.win, None, self._open_done)

    def _open_done(self, dlg, res):
        try:
            f = dlg.open_finish(res)
        except Exception:
            return
        p = Path(f.get_path())
        self.workdir = p.parent
        self._refresh_files()
        self._open_path(str(p))

    def _open_path(self, path):
        if not path:
            return
        text = Path(path).read_text(encoding="utf-8", errors="replace")
        self._set_text(text)
        self.path = path
        self.win.set_title("Vulcan IDE — " + Path(path).name)
        self._touch_status()

    def _on_save(self, *_):
        if self.path:
            Path(self.path).write_text(self._get_text(), encoding="utf-8")
            self.status.set_text("saved " + self.path)
            self._refresh_files()
            return
        dlg = Gtk.FileDialog(title="Save .vul")
        dlg.save(self.win, None, self._save_done)

    def _save_done(self, dlg, res):
        try:
            f = dlg.save_finish(res)
        except Exception:
            return
        path = f.get_path()
        Path(path).write_text(self._get_text(), encoding="utf-8")
        self.path = path
        self.workdir = Path(path).parent
        self.win.set_title("Vulcan IDE — " + Path(path).name)
        self._refresh_files()

    def _on_new(self, *_):
        self._stash_if_named()
        n = 1
        while True:
            p = self.workdir / ("untitled%d.vul" % n)
            if not p.exists():
                break
            n += 1
        p.write_text(DEFAULT_SRC, encoding="utf-8")
        self._refresh_files()
        self._open_path(str(p))


def main():
    app = VulcanIde()
    return app.run(sys.argv[:])


if __name__ == "__main__":
    sys.exit(main() or 0)
