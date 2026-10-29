# Luban-Lite Display Lab

> [中文版](README.zh-CN.md)

Panel bench tools for the ArtInChip Luban-Lite SDK (RT-Thread, the SDK's own LVGL 9 package `lvgl-ui`
and the MPP framebuffer API). They run on the SDK as it ships: no SDK source changes and no
dependency on any application's API. They are bench tools, not product features: keep them out
of release firmware.

| Shell command | What it does |
|---|---|
| `hmi_color` | Prints and changes the display-engine gamma and CCM tables, live and RAM-only |
| `hmi_testcard` | Draws a colour test card (grid or single patch) on `lv_layer_top()` over the running UI |
| `hmi_display` | Pauses LVGL drawing so the official AiPQ tool and `test_fb -b` own the screen |
| `tools/display_lab_tuner.py` | Host GUI: CCM sliders plus test-card buttons over one serial port |

The command names are the ones the luban-lite-hmi Framework used, so existing bench scripts and the
tuner keep working.

## Install

Clone the repository into the SDK, next to the other applications:

```sh
git clone https://github.com/boa-z/luban-lite-display-lab.git application/rt-thread/luban-lite-display-lab
```

**As the application.** Set `CONFIG_PRJ_APP="luban-lite-display-lab"` in a board defconfig (with
`LPKG_MPP`, `LPKG_USING_LVGL` and `LVGL_V_9`), then `scons --apply-def=<defconfig>` and `scons`.
The SDK starts LVGL, display and touch; menu "Luban-Lite Display Lab" switches each tool and sets
extra test-card presets. Use this to bring up a panel without any product code.

**Inside another application.** Call this SConscript from the application's SConscript with a
path relative to it, so objects land in `output/`:

```python
group += SConscript(os.path.join('..', 'luban-lite-display-lab', 'SConscript'))
```

The host's menu has no Display Lab entries in this mode (the SDK's Windows menuconfig cannot
source an optional file), so every tool the SDK configuration supports is built. Extra test-card
presets come from the `DISPLAY_LAB_TESTCARD_EXTRA` environment variable at build time, for example
`DISPLAY_LAB_TESTCARD_EXTRA="0x33AA55 0xEE7700"`. luban-lite-hmi lists components in Kconfig
`AIC_HMI_EXTRA_COMPONENTS` (or `HMI_EXTRA_COMPONENTS` for one build), e.g.
`HMI_EXTRA_COMPONENTS=../luban-lite-display-lab scons`.

How it attaches: the LVGL tools must run in the SDK LVGL thread. The SConscript links with
`-Wl,--wrap=lv_timer_handler`; on the first pass of that thread's loop with a display present,
`src/display_lab.c` creates the test-card and display-bench poll timers. Shell commands only post a
request; the LVGL thread applies it.

## hmi_color

```text
hmi_color                       gamma, CCM and the boot default table
hmi_color 50..300               gamma percent (100 = display engine default)
hmi_color sat 50..200           CCM from saturation (100 = identity)
hmi_color ccm                   CCM table
hmi_color ccm r|g|b C0 C1 C2 OFF  one row: Q8 gains, 9-bit offset
hmi_color ccm reset             back to the boot default
hmi_color ccm identity          no correction
```

Gamma and CCM belong to the display engine and act on the blended picture, so the UI layer and the
test card are corrected alike (the SDK brightness/contrast/saturation only touch the video layer).
On first use the command reads back the tables the application loaded at boot (for luban-lite-hmi,
`AIC_HMI_DISPLAY_GAMMA`/`AIC_HMI_DISPLAY_CCM`); `reset` returns to them. Changes are lost on reboot:
bake a result into the application's own startup configuration. Rows are set one at a time because
FinSH limits a line to 80 characters and 10 arguments.

## hmi_testcard

| Command | Result |
|---|---|
| `hmi_testcard` | state, preset list (index and `0xRRGGBB`) and usage |
| `hmi_testcard grid` | grid overview, 12 swatches per page; `next`/`prev` flip pages |
| `hmi_testcard patch 0xRRGGBB` | one full-screen patch of that exact colour |
| `hmi_testcard patch INDEX` | patch of the preset at that index |
| `hmi_testcard next` / `prev` | grid: page; patch: next/previous preset |
| `hmi_testcard off` | close the card |

Built-in presets: grayscale `#000000 #202020 #404040 #606060 #808080 #C0C0C0 #FFFFFF`, RGB and CMY;
up to 16 extra colours come from the build configuration. The UI keeps running underneath and
returns unchanged after `off`; touch is absorbed while the card is open. Patch mode draws a 65% x
62.5% patch (520 x 300 on 800 x 480) on `#202020` with a `#808080` reference block; all text stays
outside both sampling areas. Compare photos under the same light and camera settings.

## hmi_display and AiPQ

`bg_color` hides the UI foreground until the next LVGL pan, and `test_fb -b` writes framebuffer 0
that LVGL then overwrites. `hmi_display pause` drains drawing, disables invalidation, pauses the
display refresh and the first pointer input's read timer, and selects framebuffer 0. Other LVGL
timers and the application's threads keep running. The SDK asynchronous rotated display path is
rejected with an error. Enable the SDK commands (`AIC_MPP_FB_TEST`, `AIC_DISPLAY_TEST`) as needed.

1. Turn off Color Bar in AiPQ, disconnect it, run `hmi_display pause` in a terminal.
2. Wait for `display=paused` (or `hmi_display` shows `display=paused pending=0`; `pending=1` is
   only queued, a failure logs `display request failed`).
3. Close the terminal, connect AiPQ, use Pure Color and Gradient.
4. If Gradient stays hidden after Pure Color: disconnect AiPQ, run `hmi_display fb`, wait for
   `display=paused framebuffer=0`, then `test_fb -b`.
5. When done: Color Bar off, disconnect AiPQ, `hmi_display resume`, wait for `display=app`. The UI
   layer is restored, the whole screen redrawn and pointer input resumed.

`pause`, `fb` and `resume` are RAM-only and do not touch gamma/CCM.

## Tuner

```sh
python -m pip install -r tools/requirements.txt
python tools/display_lab_tuner.py --port COM3
```

Sliders and entries per CCM row (row sum shown, 256 keeps white), gamma and saturation entries,
and a test-card section (grid, prev/next, patch by index or colour, off). Needs pyserial and Tk.

## Host checks

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Gamma/CCM tables, test-card geometry and display-bench behaviour against the SDK LVGL source
(`-DDISPLAY_LAB_LVGL_ROOT=` outside the SDK tree), and the tuner logic. They do not replace a panel
check on the board.

## License

Apache-2.0, see [LICENSE](LICENSE).
