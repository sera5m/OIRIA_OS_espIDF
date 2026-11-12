a custom smartwatch operating system designed to integrate with a variety of external hardware in the tasks:
-normal smartwatch use
-flipper zero like utilities (no skiddie bullshit)
-remote controll and wide communication (lora/bt/wifi/infared/what you connect)
-soon: external hardware support for random stuff+osciliscope+electronic toolkit
-soon: shell cmds/elif/etc
--soon: multi-esp32 colaboration over uart so you can be goated
-partially working: microsd storage; will add unix-like cmd's soon?

the general goal is to have a mini cyberdeck thingy/non shitty smartwatch.
far too many smartwatches focus on being a second, shittier phone strapped to your wrist.
 why not do something different


currently working:
lcd and window enviroment: benchmarked at 100fps on 240*280; limited to 45 fps via configuration....
....might need a minor revision (it is double buffered, which eats a whole megabyte of the 16mb psram) 
i've done a lot of hacks to keep the ram free, but when i add the document viewer app i may try doing things like having a pointer to the document, and only loading what you see and then a little more, like swap space. or else this would be of very poor quality and lag. 

applications: we are ALMOST there gng
input handler: only buttons and knobs so far. keyboard/mouse/touchscreen/using your phone soon-ish

computer

---

# How the OS works

Manual page for the runtime. Describes what is actually in the tree, not the intended end state.

```
OIRIA_OS(7)                  OIRIA OS Manual                  OIRIA_OS(7)

NAME
       oiria_os — cooperative, app-centric runtime for the ESP32-S3 watch

SYNOPSIS
       FreeRTOS tasks under everything.
       Singletons for the two managers.
       Apps own their windows.
       Input is an event, not a driver tree.
       Data is a DataItem when it needs to move.
       The VM asks the host; the host is not a kernel.

DESCRIPTION
       OIRIA_OS is not a kernel in the Unix sense. There is no user/kernel
       boundary, no process table with address spaces, and no enforced
       capability check. What exists is a set of cooperating FreeRTOS tasks
       that share one address space, two long-lived managers, and a
       convention about who owns what.

       The philosophy is: keep the machinery small, put the policy in the
       app, and let a manager only borrow what it must. A window is not a
       system object that an app requests. An app builds a Window and hands
       it to WindowManager. Focus is not a lock. It is a pointer plus a
       flag plus a layer number. Input is not a device node. It is a packed
       event pushed onto a queue and routed by a target enum.

       Where a Unix system would grow a syscall table, this one grew two
       thinner things: an empty hook on AppBase, and a set of host function
       pointers the Vulcan VM calls when it executes a SYS opcode. The
       first is unused. The second is the real syscall surface, and it
       only exists for code running inside the VM.

PHILOSOPHY
       1. One address space. Isolation is by convention, not by the MMU.
       2. Ownership stays with the app. Managers borrow.
       3. Dirty flags over redraws. Draw only what changed.
       4. Events over polling. The input task produces; apps consume.
       5. Dataflow is the intended long-term currency (DataItem, pipe,
          pool), but most apps still talk to their window directly.
       6. Capabilities are declared, not enforced. AppCapability is a
          bitmask an app advertises. Nothing checks it before a GPIO write.

SYS_CALLS(2)
       There is no central syscall table for native apps.

       AppBase declares:

           virtual void on_syscall(uint32_t syscall_id, void* data) {}

       Nothing in the tree calls it. It is a reserved hook.

       The real syscall surface lives in the Vulcan VM. Opcodes 0xE0–0xEE
       are the SYS family. Each one pops arguments off the VM stack and
       calls a function pointer on the host struct. If the host did not
       install the pointer, the opcode pushes nil or -1 and continues.

           RSVM_OP_SYS_CMD    0xE0   host.sys_cmd(cmd, buf)
           RSVM_OP_SYS_EXEC   0xE1   host.sys_exec(path)
           RSVM_OP_SYS_OPEN   0xE2   host.sys_open_app(name)
           RSVM_OP_SYS_LS     0xE3   host.sys_ls(path, buf)
           RSVM_OP_SYS_CD     0xE4   host.sys_cd(path)
           RSVM_OP_SYS_PWD    0xE5   host.sys_pwd(buf)
           RSVM_OP_SYS_ENV    0xE9   host.sys_env
           RSVM_OP_SYS_GREP   0xEA   host.sys_grep
           RSVM_OP_SYS_CAT    0xEB   host.sys_cat
           RSVM_OP_SYS_ECHO   0xEC   host.sys_echo
           RSVM_OP_SYS_HEAD   0xED   host.sys_head
           RSVM_OP_SYS_WC     0xEE   host.sys_wc

       A second family, MW_*, talks to the window system the same way
       (mw_set_text and friends). The VM never touches a Window. It asks
       the host, and the host decides what that means on this machine.

       This is deliberate. The firmware is an interpreter. "Syscall" here
       means "escape the interpreter into a function the host installed,"
       not "trap into a kernel."

APPS(7)
       An app is a subclass of AppBase plus an AppManifest.

       The manifest is the registry entry: name, display name, description,
       capabilities, stack size, priority, tick rate, and a factory:

           m.create = [](const ApplicationConfig& cfg) {
               return std::make_shared<HealthApp>(cfg);
           };

       register_app() stores the manifest. open_app() looks it up, calls
       the factory, then init() and start_task(). start_task() is an
       xTaskCreate of task_func, which loops:

           while (!should_stop_) {
               if (!paused_) tick_app(delta_ms);
               esp_task_wdt_reset();
               vTaskDelay(tick_period);
           }

       tick_rate_hz sets the delay. A health app at 4 Hz wakes every
       250 ms. A scope at 8 Hz wakes faster. The app does its work in
       tick_app() and returns. It does not block the loop.

       Input arrives on a different path. The input task calls
       route_input_to_focused(), which calls the focused app's
       receive_event_input(). The app sees a const InputEvent*. It does
       not poll buttons.

       Lifecycle hooks, all optional:

           on_start / on_stop
           on_pause / on_resume
           on_draw
           on_stream_data     (DataItem from a pipe)
           on_syscall         (unused)

       Capabilities (MINIMIZABLE, NEEDS_WINDOW, USES_SD_CARD,
       STREAM_IN_CAPABLE, TOPLETHOS_HOST, ...) are a declaration. The
       manager records them. It does not refuse an app that draws without
       NEEDS_WINDOW, or writes the SD without USES_SD_CARD.

       close_current_and_open(name) is the usual way one app yields to
       another. It is not a fork/exec. It is "stop looking at me, open
       that."

WINDOW(7)
       A Window is owned by the app. WindowManager only holds a
       shared_ptr in a vector.

       A window has:

           position, size, layer
           text (SetText), optional Canvas
           dirty / content_dirty
           window_highlighted
           colors, text scale, background tile

       WinDraw() is the draw. If the window is not highlighted and not
       dirty and has no refresh override, it returns immediately. Otherwise
       it tokenizes the text, paints the background, and draws. Dirty is
       cleared at the end.

       Canvas is a fixed shape buffer (FB_MAX_SHAPES, 256) allocated once
       in PSRAM. AddShape writes into shapes[count]. ClearShapes sets
       count back to 0. Shapes are reused, not freed and reallocated.
       This is why a live bar graph can redraw twice a second without
       growing.

       Layers are the z-order. SortWindowsByLayer() orders the vector.
       The focused app's window is raised to layer 255 and marked
       highlighted. The highlight is what gates the dashed focus border.

       Fullscreen is a saved state: toolbar off, old clamp remembered,
       one window stretched. restore_from_fullscreen() puts it back.

WINDOWMGR(7)
       WindowManager is a singleton. getInstance() returns it.

       UpdateAll() is the compositor frame. It is called from the display
       push task, not from the app. It:

           samples the focus epoch
           prunes dead windows
           optionally repositions
           draws each window (WinDraw)
           draws the toolbar
           acks the focus epoch

       The epoch exists so set_focused_app() does not have to wait for
       the frame it just requested. It bumps a counter. UpdateAll samples
       it at the start and acks it at the end. wait_focus_settled() can
       block until that ack if a caller cares.

       The manager does not create windows. registerWindow() takes one
       the app already built. unregisterWindow() removes it. PruneDeadWindows()
       drops expired shared_ptrs.

       Toolbar is part of the manager, not an app: icons, rotation, text,
       drawn on top or under depending on the UpdateAll flag.

INPUT(7)
       Input is a producer-consumer pair.

       Producers: button driver, KY-040 encoder, USB HID host, rubber
       ducky parser. Each one builds an InputEvent:

           key          KEY_ENTER, KEY_BACK, KEY_UP, KEY_DOWN, ...
           action       Tap, Hold, Release, Repeat, PositionDelta
           source       Button, Knob, Mouse, Keyboard, Virtual
           delta        for knobs and pointers
           timestamp
           target       HIDTarget

       The event is pushed to ProcInputQueTarget, a FreeRTOS queue.

       The consumer is the input processor task (ipt_x). It pops the
       queue and calls RouteInput_HidTarget(). The target enum decides
       the sink:

           toTask              focused app
           toTaskAndDebug      focused app + log
           toStreamCore        DataItem into the pump
           actAsUsbHID         out as USB HID
           wireless_hid        (TODO)
           everything          all of the above

       v_env.CurrentHIDTarget is the global default. Boot sets it to
       toTaskAndDebug. An app does not register for keys. It is focused,
       and the route sends the event to whoever is focused.

       This is why KEY_BACK in HealthApp can call
       close_current_and_open("MenuApp"). The app received the event
       because it was focused. The menu will receive the next one because
       focus moved.

RSHELL(7)
       rShell is the app runtime and the dataflow skeleton. It is not a
       shell you type into, though the VM can fake one.

       Pieces:

           AppBase / appManager
               registry, launch, focus, task lifecycle

           streams/
               DataItem   timestamp + source + tags + payload
               RshellPipe source → targets, owned by appManager
               DataPool   a byte buffer in a storage class
                          (RAM, PSRAM, NVS, SD, external)
               pump       a FreeRTOS task that pulls DataItems and
                          routes them by a compatibility map

       The intended shape is Unix pipes plus shared PSRAM:

           source app  →  pipe  →  sink app
                                 →  screen
                                 →  SD (CBOR)
                                 →  notification

       High-rate paths are supposed to skip the queue and share a PSRAM
       ring. That path is declared (ST_RING_CAPABLE) more than it is used.

       What is real today:

           apps launch, tick, receive input, own windows
           focus moves, highlight follows
           pipes can be created and held
           the pump task exists

       What is still a stub:

           CBOR serialize + SD write in the pump
           notifications as a sink
           several storage types fall back to RAM
           the compatibility map is thinner than the header suggests

       env_vars (v_env) is the process environment: HID target, storage
       sizes, wireless credentials, the things a Unix system would put
       in environ. It is a struct, loaded from /sdcard/conf/os.conf or
       an embedded blob, not a set of strings.

OPERATION STYLE
       Boot brings up NVS, the rescue window, the role (SOLO / TYRANT /
       PUPPET), then the managers. Register_appTable() fills the manifest
       map. If the role runs apps locally, WatchApp is opened. Everything
       after that is apps calling close_current_and_open, or the VM
       calling sys_open_app.

       A frame is:

           input task pops an event
           focused app handles it in receive_event_input
           app's own task wakes, runs tick_app
           app sets dirty or ClearShapes/AddShape
           display task calls UpdateAll
           WinDraw paints what is dirty

       No app draws to the panel directly in the normal path. It dirties
       a window. The compositor paints.

       Tyrant/puppet does not change this model. A puppet still runs the
       app and marks windows dirty. A small emit task packs the frame and
       sends it over UART. The tyrant draws what it receives. The app
       does not know.

SEE ALSO
       architectureGoal.md    the dataflow that is only partly built
       APPMANAGER_FUSE.md     how the VM is and is not linked
       INTEROP.md              live memory interop, not implemented

       The short version: apps are tasks that own windows, input is a
       queue routed to whoever is focused, drawing is dirty flags and a
       compositor, and the only real syscalls are the ones the VM makes
       when it wants the host to do something.

OIRIA_OS                          2026-11-12                     OIRIA_OS(7)
```
