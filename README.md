# Hikvision 配置工具

用于本地配置文件的加密、解密、只读预览和另存为。

## 用法

打开或拖入本地配置文件，选择加密或解密，再将结果另存为。输入文件和转换结果均可只读预览，支持十六进制、UTF-8 文本和可连续滚动的字符串列表。双击字符串可跳到对应的十六进制位置。

## 构建与测试

需要 CMake 3.27+、C++17 编译器和 wxWidgets 3.2.12 或更高版本。Windows 可使用 Visual Studio 2022 的 C++ 工具集和对应架构的 wxWidgets SDK；将头文件、开发库和运行时 DLL 解压到同一目录。

```powershell
cmake -S . -B build/windows -A x64 -DwxWidgets_ROOT_DIR="C:/deps/wxWidgets-3.2.12" -DHIKVISION_GUI_SMOKE_TESTS=ON
cmake --build build/windows --config Release
ctest --test-dir build/windows -C Release --output-on-failure
```

也可以将 `WXWIN` 指向 SDK 目录后用 Visual Studio 打开根目录的解决方案；建议优先使用 CMake 构建。

GUI 测试使用合成文件，需要桌面环境，默认关闭。不构建 GUI 时可指定 `-DHIKVISION_BUILD_GUI=OFF`，无需安装 wxWidgets。

本项目沿用 WormChickenWizard 的 Hikvision Decrypter 和 Sergey Bel 的 AES 实现。许可见 `LICENSE`。
