# Luban-Lite Display Lab

> [English](README.md)

匠芯创 Luban-Lite SDK 的面板台架工具，直接运行在 SDK 原生的 RT-Thread、LVGL 9（`lvgl-ui` 包）和 MPP
framebuffer 接口上：不改 SDK 源码，不依赖任何应用的 API。它们是台架工具，不是产品功能，正式固件不要带。

| Shell 命令 | 作用 |
|---|---|
| `hmi_color` | 查看并实时修改显示引擎的 gamma、CCM 表，只在 RAM |
| `hmi_testcard` | 在 `lv_layer_top()` 上画测试色卡（网格或单色），覆盖在运行中的界面上 |
| `hmi_display` | 暂停 LVGL 绘制，把屏幕交给官方 AiPQ 工具与 `test_fb -b` |
| `tools/display_lab_tuner.py` | 上位机：同一个串口上的 CCM 滑杆与色卡按钮 |

命令名沿用 luban-lite-hmi Framework 原来的名字，已有的台架脚本和上位机无需改动。

## 安装

克隆到 SDK 里，与其他应用并列：

```sh
git clone https://github.com/boa-z/luban-lite-display-lab.git application/rt-thread/luban-lite-display-lab
```

**作为应用**：板级 defconfig 设 `CONFIG_PRJ_APP="luban-lite-display-lab"`（并启用 `LPKG_MPP`、
`LPKG_USING_LVGL`、`LVGL_V_9`），然后 `scons --apply-def=<defconfig>`、`scons`。SDK 负责启动 LVGL、显示和
触摸；菜单 "Luban-Lite Display Lab" 可单独开关每个工具、设置色卡追加色。适合不带任何产品代码点亮面板。

**作为另一个应用的组件**：在该应用的 SConscript 里用相对路径调用本仓库的 SConscript，对象才会落进 `output/`：

```python
group += SConscript(os.path.join('..', 'luban-lite-display-lab', 'SConscript'))
```

这种方式下宿主菜单里没有 Display Lab 的选项（SDK 的 Windows menuconfig 不支持可选 source），SDK 配置支持的
工具全部编译；色卡追加色由构建时的环境变量 `DISPLAY_LAB_TESTCARD_EXTRA` 给出，例如
`DISPLAY_LAB_TESTCARD_EXTRA="0x33AA55 0xEE7700"`。luban-lite-hmi 在 Kconfig `AIC_HMI_EXTRA_COMPONENTS`
里列组件目录（单次构建可用环境变量 `HMI_EXTRA_COMPONENTS`），例如
`HMI_EXTRA_COMPONENTS=../luban-lite-display-lab scons`。

接入原理：LVGL 侧的工具必须在 SDK 的 LVGL 线程里运行。SConscript 链接时加 `-Wl,--wrap=lv_timer_handler`，
该线程主循环第一次进入且显示已建好时，`src/display_lab.c` 挂上色卡与显示接管的轮询定时器。shell 命令只写请求，
由 LVGL 线程执行。

## hmi_color

```text
hmi_color                       gamma、CCM 与开机默认表
hmi_color 50..300               gamma 百分比（100 = 显示引擎默认）
hmi_color sat 50..200           按饱和度生成 CCM（100 = 单位阵）
hmi_color ccm                   CCM 表
hmi_color ccm r|g|b C0 C1 C2 OFF  设置一行：Q8 增益、9 位偏移
hmi_color ccm reset             回到开机默认表
hmi_color ccm identity          不做校正
```

gamma 与 CCM 属于显示引擎，作用在混合后的整幅画面上，UI 图层与色卡同样被校正（SDK 的亮度/对比度/饱和度只作用于
视频层）。第一次执行命令时读回应用开机加载的表（luban-lite-hmi 对应 `AIC_HMI_DISPLAY_GAMMA`/`AIC_HMI_DISPLAY_CCM`），
`reset` 回到它。改动断电即丢失：调好的值写回应用自己的启动配置。逐行设置是因为 FinSH 单行限 80 字符、10 个参数。

## hmi_testcard

| 命令 | 结果 |
|---|---|
| `hmi_testcard` | 当前状态、预置色表（序号与 `0xRRGGBB`）和用法 |
| `hmi_testcard grid` | 网格总览，每页 12 块；`next`/`prev` 翻页 |
| `hmi_testcard patch 0xRRGGBB` | 指定颜色的整屏色块 |
| `hmi_testcard patch INDEX` | 按预置序号显示色块 |
| `hmi_testcard next` / `prev` | 网格翻页；单色切换下一个/上一个预置色 |
| `hmi_testcard off` | 关闭色卡 |

内置预置为灰阶 `#000000 #202020 #404040 #606060 #808080 #C0C0C0 #FFFFFF`、RGB、CMY；最多 16 个追加色来自构建配置。
界面照常在下面运行，`off` 后原样返回；色卡打开时触摸被色卡吸收。单色模式在 `#202020` 背景上画 65% × 62.5% 的色块
（800 × 480 屏为 520 × 300），同框 `#808080` 灰参考块，文字都在两个采样区之外。拍照对比请固定光照与相机设置。

## hmi_display 与 AiPQ

`bg_color` 隐藏的 UI 前景会在下一次 LVGL 翻帧时重新打开，`test_fb -b` 写的缓冲 0 会被 LVGL 覆盖。
`hmi_display pause` 排空已有绘制、禁用 invalidation、暂停显示刷新与首个指针设备的读取定时器，并选中缓冲 0；
其他 LVGL 定时器和应用线程照常运行。SDK 的异步旋转显示路径会明确报错。SDK 自带命令按需开启
（`AIC_MPP_FB_TEST`、`AIC_DISPLAY_TEST`）。

1. AiPQ 先关闭 Color Bar 并断开，用终端执行 `hmi_display pause`。
2. 等待 `display=paused`（或 `hmi_display` 显示 `display=paused pending=0`；`pending=1` 只是排队，失败日志为
   `display request failed`）。
3. 关闭终端，连接 AiPQ，使用 Pure Color 与 Gradient。
4. Pure Color 之后 Gradient 不显示：断开 AiPQ，执行 `hmi_display fb`，等待 `display=paused framebuffer=0`，再执行 `test_fb -b`。
5. 结束：关闭 Color Bar、断开 AiPQ，执行 `hmi_display resume`，等待 `display=app`。UI 图层还原、整屏重绘、触摸恢复。

`pause`、`fb`、`resume` 只改 RAM，不碰 gamma/CCM。

## 上位机

```sh
python -m pip install -r tools/requirements.txt
python tools/display_lab_tuner.py --port COM3
```

每行 CCM 的滑杆与输入框（显示行和，256 保持白点）、gamma 与饱和度输入、测试色卡区（网格、上/下一色、按序号或色值
单色、关闭）。需要 pyserial 与 Tk。

## Host 检查

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

检查 gamma/CCM 表、色卡几何、显示接管行为（基于 SDK 的 LVGL 源码，不在 SDK 树内时传
`-DDISPLAY_LAB_LVGL_ROOT=`）与上位机逻辑；不能代替实板看屏。

## 许可

Apache-2.0，见 [LICENSE](LICENSE)。
