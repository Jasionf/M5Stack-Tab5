# 🎮 M5Stack Tab5 趣味开发套件
<p align="center">
  <img src="https://m5stack.oss-cn-shenzhen.aliyuncs.com/image/m5-docs/core/stack_tab5_01.webp" width="400" alt="M5Stack Tab5"/>
</p>

> ✨ 基于ESP32-P4的高性能触摸开发板趣味应用集合，让你的Tab5秒变多功能娱乐终端！

---

## 📦 项目总览

这个仓库包含两个超有意思的M5Stack Tab5应用，都是基于LVGL图形库开发的，开箱即用：

| 🎮 游戏项目 | 💻 终端项目 |
|------------|------------|
| 霓虹俄罗斯方块 | 智能交互终端 |
| 触摸手势操作 | 无线联网支持 |
| 流畅动画效果 | 炫酷开机动画 |
| 完整游戏逻辑 | 可扩展功能界面 |

---

## 🕹️ 项目一：NEON 霓虹俄罗斯方块
<p align="center">
  <img src="https://media.giphy.com/media/3o7qE1YN7aBOFPRw8E/giphy.gif" width="300" alt="俄罗斯方块演示"/>
</p>

### ✨ 特色功能
- 🎨 **霓虹发光风格UI**：赛博朋克既视感的游戏界面
- 🖐️ **触摸手势控制**：左右滑动移动、点击旋转、下拉加速
- 🎯 **完整游戏逻辑**：消行、计分、等级提升一应俱全
- ⚡ **高性能渲染**：基于ESP32-P4 + LVGL，丝滑60fps无卡顿
- 🔊 **音效支持**：（可扩展）游戏操作音效和BGM

### 📂 代码结构
```
Game/
├── main/
│   ├── tetris_engine.c    # 俄罗斯方块核心逻辑
│   ├── tetris_view.c      # 界面渲染
│   ├── game_controller.c  # 游戏控制逻辑
│   ├── input_uart.c       # 输入处理
│   └── ui_*.c             # 各种UI界面
├── sdkconfig              # 编译配置
└── CMakeLists.txt         # 工程配置
```

---

## 🖥️ 项目二：智能交互终端
<p align="center">
  <img src="https://media.giphy.com/media/xT0Gqn9yuw8hn67Uli/giphy.gif" width="300" alt="终端演示"/>
</p>

### ✨ 特色功能
- 🎬 **炫酷开机动画**：流畅的SVG加载动效
- 🛜 **Wi-Fi联网支持**：内置无线管理模块
- 📱 **触摸交互**：支持多点触摸和手势识别
- 🔌 **可扩展接口**：预留丰富的功能扩展位
- 🎨 **LVGL组件库**：可以快速开发各种界面

### 📂 代码结构
```
Terminal/
├── main/
│   ├── example_lvgl_demo_ui.c  # 演示UI
│   ├── wireless_mgr.c          # 无线管理
│   └── uart_mgr.c              # 串口管理
├── wifi_c6_fw/             # Wi-Fi固件
├── svg-spinners/           # SVG动效资源
└── CMakeLists.txt          # 工程配置
```

---

## 🚀 快速开始

### 硬件要求
- 🛠️ M5Stack Tab5 开发板
- 🔌 USB-C 数据线
- 💻 电脑 (Windows/macOS/Linux)

### 环境搭建
1. 安装 [VS Code](https://code.visualstudio.com/)
2. 安装 [ESP-IDF 扩展](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
3. 克隆本仓库到本地

### 编译烧录
1. 打开对应项目文件夹（Game或者Terminal）
2. 连接M5Stack Tab5到电脑
3. 点击VS Code底部的「⚡ 烧录」按钮
4. 等待烧录完成，设备会自动重启

---

## 🎯 玩法说明

### 俄罗斯方块玩法
- 👈 向左滑动：方块左移
- 👉 向右滑动：方块右移
- 👇 向下滑动：方块加速下落
- 👆 点击屏幕：方块旋转
- 🏆 消行越多分数越高，等级越高速度越快

### 终端功能
- 🔄 开机自动播放动画
- 📶 自动搜索连接Wi-Fi（可配置）
- 🖐️ 触摸屏幕可以交互测试
- 🔧 可扩展开发各种智能应用

---

## 🛠️ 自定义开发

### 如何修改俄罗斯方块皮肤？
编辑 `Game/main/tetris_view.c` 中的颜色宏定义，就可以自定义方块颜色和界面风格啦！

### 如何添加新功能到终端？
在 `Terminal/main/example_lvgl_demo_ui.c` 中添加LVGL组件即可，LVGL官方有非常多好看的组件可以直接用。

### 动图替换指南
你可以把上面的示例动图替换成自己的实际演示动图，放在 `docs/` 文件夹下，然后修改README里的图片链接就行！

---

## 📝 技术栈
| 技术 | 用途 |
|------|------|
| ESP-IDF v5.x | 物联网开发框架 |
| LVGL v9.x | 嵌入式图形库 |
| ESP-BSP | 板级支持包 |
| C语言 | 主要开发语言 |
| CMake | 构建系统 |

---

## 🤝 贡献
欢迎提交Issue和PR！有什么好点子也可以一起讨论~

---

## 📄 许可证
本项目基于 CC0-1.0 许可证开源，随便玩随便改~

---
<p align="center">
  Made with ❤️ for M5Stack Tab5
</p>