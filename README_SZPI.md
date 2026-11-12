# Pocket Tank · 立创实战派 ESP32-S3

本工程基于 mediacutlet/pocket-tank 的 `952ef4d`，对应上游 v0.4.1 alpha，目标开发板为立创实战派 ESP32-S3 N16R8，开发环境为 ESP-IDF 6.0.2。

公共板卡资料位于 [父目录的资料库](../board-support/szpi-esp32s3/README.md)。上游授权和作者声明保存在 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

日常使用请阅读 [中文操作手册](docs/操作手册.md)。

## 构建

在本工程根目录打开终端并执行。

```console
python tools/idf_szpi.py build
```

此入口读取本机 Espressif Installation Manager 的 `C:/Espressif/tools/eim_idf.json`，选用已注册的 v6.0.2，并校验实际版本。若注册表位于其他位置，可设置 `IDF_EIM_REGISTRY` 环境变量。入口直接读取 EIM 的环境值，适用于本机 PowerShell 启动失败的情况。

构建目录为 `firmware/build-szpi`，生成配置为 `firmware/sdkconfig.szpi.local`，默认配置按顺序加载 `sdkconfig.defaults` 和 `sdkconfig.szpi`。模型已经随上游仓库下载，文件为 `model/out/model_q4.bin`，大小为 7,557,640 字节。

已激活 IDF 6.0.2 的终端也可执行以下命令。

```console
idf.py -C firmware -B firmware/build-szpi -D SDKCONFIG=sdkconfig.szpi.local -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.szpi" -D PT_LCD_TEST=OFF build
```

## 烧录与查看日志

连接开发板的 USB Type-C 接口，使用设备管理器实际显示的串口号替换 `COMx`。

```console
python tools/idf_szpi.py -p COMx flash monitor
```

`flash` 包括引导程序、分区表、应用、OTA 初始数据和 `0x290000` 地址的模型。烧录完成后自动复位，串口日志速率为 115200。退出 IDF monitor 使用 Ctrl+]。通过板载 USB HUB，CH340K 和原生 USB Serial/JTAG 可同时枚举。本板的交互调试命令使用 CH340K 所连接的 UART0。

本地首次构建生成独立 RSA-3072 签名密钥 `firmware/keys/ota_signing_key.pem`，用于沿用上游的签名应用构建流程。此文件已加入忽略规则，后续构建复用同一文件。请随工程做好密钥备份。烧录产物中的板卡标识为 `szpi-s3`。

## 独立 LCD 验证

`lcd_official_test.c` 按官方第 9 章的 SPI3、80 MHz、模式 2 和初始化顺序显示固定四色，使用 IDF 6 的 I²C 主机接口。测试程序保持功放关闭。以下命令仅更新应用。

```console
python tools/idf_szpi.py -D PT_LCD_TEST=ON -p COMx app-flash
```

普通命令会选择鱼缸应用，恢复时执行 `python tools/idf_szpi.py -p COMx app-flash`。本板调试命令通过 CH340K 所连接的 UART0 接收，例如 `state`、`snd off`、`touch log on`。完整首次烧录使用前面的 `flash` 命令。

## 板级适配

- 使用板载 ST7789、SPI3、GPIO40/41/39，以及 GPIO42 低有效 PWM 背光。
- 使用 GPIO1/2 上的共享 I²C0。PCA9557 地址为 0x19，P0 控制 LCD 片选，P1 控制音频功放，P2 保持摄像头关闭。
- 使用官方 FT5x06 驱动读取板载触摸。触摸复用上游完整手势和页面逻辑，包括投食、鱼卡、商店、清洁和修剪。
- ES8311 固定使用本板的 0x18 地址，I²S MCLK/BCLK/WS/DOUT 分别为 GPIO38/14/13/45。音效混音及音量存储沿用上游实现。
- 本板默认音量为 0，首次启动保持静音。设置中调整后的音量会保存，后续启动使用保存值。
- N16R8 配置为 16 MB Flash、8 MB Octal PSRAM、240 MHz CPU。LLM 模型从独立分区映射，鱼缸玩法与推理主体基于上游实现。
- 本板采用 480×360 的 4:3 场景坐标，文字和鱼体使用 320×240 原生像素栅格绘制。触摸按同一映射转换，页面采用 448×360 布局并居中。
- 本板新鱼缸最多养 3 条鱼，初始为 2 条。鱼体增加平滑轮廓、渐变光照、鳍条、鳃盖、眼睛高光与胸鳍摆动，实际游动的鱼放大约 35%。
- 本板使用双 DMA 条带缓冲，帧率目标为 30 FPS。转向使用角速度限制与头部阻尼过渡，靠近侧边玻璃时提前减速。日常高频性能日志默认关闭，可用 `fps reset` 和 `fps` 查看实际帧间隔。
- 中文界面使用 Fusion Pixel 像素字库，覆盖界面文案及常用 GB2312 汉字。字体和上游授权位于 `assets/fonts/fusion-pixel/`。翻译目录为 `localization/zh_CN.tsv`，字库及文案由 `tools/localize_ui.py` 生成。鱼缸库、海马状态、系统家园名称及版本信息已补齐中文。默认鱼缸名称在显示时汉化，自定义鱼名与鱼缸名沿用存档，命名输入使用字母滚轮。
- 本板电源为 USB 供电，硬件没有上游使用的 AXP2101 和 PCF85063。上游端口在芯片缺席时返回缺席状态，BOOT/GPIO0 承担休眠控制，软件保留 NVS 存档和 ESP32 时钟路径。

IDF 6.0 的内置 `json` 组件已迁移到组件管理器中的 `espressif/cjson`。SHA-256 使用 PSA Crypto，异步 DMA 配置使用 `dma_burst_size`，ESP-NOW 发送回调使用新的发送信息结构。组件版本已记录在 `firmware/dependencies.lock`。板卡具有独立固件标识，上游发布的 Waveshare 镜像与本板硬件配置不同，后续更新请继续构建本工程的 `szpi-s3` 镜像。

## 验证记录

构建、模型校验和实际完成范围见 [验证记录](verification/README.md)。屏幕方向、触摸精度、声音、帧率与长时间运行效果以连接开发板后的实测为准。
