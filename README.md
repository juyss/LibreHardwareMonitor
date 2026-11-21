# LibreHardwareMonitor
[![GitHub license](https://img.shields.io/github/license/LibreHardwareMonitor/LibreHardwareMonitor?label=License)](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LICENSE)

[![NuGet](https://img.shields.io/nuget/v/LibreHardwareMonitorLib?label=NuGet&logo=nuget&logoColor=white)](https://www.nuget.org/packages/LibreHardwareMonitorLib/)
[![NuGet (Prerelases)](https://img.shields.io/nuget/vpre/LibreHardwareMonitorLib?label=NuGet-Prerelease&logo=nuget&logoColor=white)](https://www.nuget.org/packages/LibreHardwareMonitorLib/)
[![Downloads](https://img.shields.io/nuget/dt/LibreHardwareMonitorLib?label=Downloads&logo=nuget&logoColor=white)](https://www.nuget.org/packages/LibreHardwareMonitorLib/)

---

## 本分支新增功能

本仓库在 LibreHardwareMonitor 基础上增加了 **服务端 SSE 推送** 和 **ESP8266 OLED 监控端**。

### 1. SSE 事件流接口

- 新增 `GET /events?interval=500`：通过 Server-Sent Events 持续推送数据，替代客户端轮询 `/data.json`。
- 支持 `format=compact`：只推送 OLED 需要的 7 个字段，单条约 100 字节：

  ```json
  {"cpu":"Intel Core i5-14600KF","ct":32,"cl":9,"gpu":"AMD Radeon RX 6750 XT","gt":39,"gl":18,"ram":71}
  ```

- 不带 `format=compact` 时仍推送与 `/data.json` 相同的完整 JSON，兼容旧客户端。
- 服务端每 15 秒发送心跳，客户端负责断线检测和自动重连。

### 2. ESP8266 OLED 监控端（`ESP8266/`）

- 基于 PlatformIO（`d1_mini`）和 SH1106 128×64 OLED，显示内容与原 HTTP 轮询版本一致：
  - CPU 名称、温度、负载
  - GPU 名称、温度、负载
  - 物理内存占用率（Total Memory 的 Load）
- 只解析 compact 事件，使用 `StaticJsonDocument<512>`，不再需要 12 KB 动态文档。
- 支持 chunked 响应、跳过心跳、断线 3 秒后自动重连。
- 传感器选择：CPU 取 `Core Max` / `CPU Total`，GPU 取 `GPU Core`，内存取 `/ram`（物理内存，而非虚拟内存）。
- Wi-Fi 信息放在 `ESP8266/secrets.h`（已被 `.gitignore` 忽略）；使用前从 `secrets.example.h` 复制并填写。

### 3. 流量对比（500 ms 间隔）

| 方式 | 单条数据 | 流量 |
| --- | --- | --- |
| 完整 JSON（旧版轮询 / 默认 SSE） | 约 42.6 KB | 约 85 KB/s，7.36 GB/天 |
| `format=compact` | 约 101 B | 约 0.2 KB/s，18.7 MB/天 |

### 4. 构建与运行

- 服务端：需要 .NET 10 Desktop Runtime（x64），构建命令：

  ```powershell
  dotnet build LibreHardwareMonitor.Windows.Forms/LibreHardwareMonitor.Windows.Forms.csproj -f net10.0-windows -c Release -p:Platform=x64
  ```

- ESP8266：PlatformIO 工程，依赖 ArduinoJson 和 ThingPulse SSD1306/SH1106 驱动；将 `ESP8266/secrets.example.h` 复制为 `secrets.h`，填入 Wi-Fi 与服务器地址。
- 服务端为便携版，直接运行 `LibreHardwareMonitor.Windows.Forms.exe`（需要管理员权限）。

---

Libre Hardware Monitor is free software that can monitor the temperature sensors, fan speeds, voltages, load and clock speeds of your computer. 

## What's included?
| Name| Build Status |
| --- | --- |
| **LibreHardwareMonitorLib** <br /> Library that allows you to use all features in your own application | [![Build status](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions/workflows/master.yml/badge.svg)](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions) |
| **LibreHardwareMonitor.Windows.Forms** <br /> Windows Forms based application that presents all data in a graphical interface | [![Build status](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions/workflows/master.yml/badge.svg)](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions) |
| **LibreHardwareMonitor.Avalonia** <br /> Avalonia based application that presents all data in a graphical interface <br/> _In development, not recommended yet_ | [![Build status](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions/workflows/master.yml/badge.svg)](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions) |

## What can it do?
You can read information from devices such as:
- Motherboards
- Intel and AMD processors
- NVIDIA, AMD and Intel graphics cards
- HDD, SSD and NVMe hard drives
- Network cards

## Where can I download it?
You can download the latest release [here](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases).

### Nightly builds
If you have a **GitHub** account, you can download nightly builds [here](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/actions).

Otherwise, you can download the *latest* nightly build
[here (Windows.Forms)](https://nightly.link/LibreHardwareMonitor/LibreHardwareMonitor/workflows/master/master/LibreHardwareMonitor.Windows.Forms.zip)
or [here (Avalonia)](https://nightly.link/LibreHardwareMonitor/LibreHardwareMonitor/workflows/master/master/LibreHardwareMonitor.Avalonia%20%28win-x64%29.zip).

## How can I help improve it?
The LibreHardwareMonitor team welcomes feedback and contributions!<br/>
You can check if it works properly on your motherboard. For many manufacturers, the way of reading data differs a bit, so if you notice any inaccuracies, please send us a pull request. If you have any suggestions or improvements, don't hesitate to create an issue.

### Which .NET versions are supported?

| Target | Library | Windows.Forms | Avalonia |
| --- | --- | --- | --- |
| .NET Framework 4.7.2 | ✅ | ✅ | ❌ |
| .NET Framework 4.8.1 | ✅ | ✅ | ❌ |
| .NET Standard 2.0 | ✅ | ✅ | ❌ |
| .NET 10 | ✅ | ✅ | ✅ |
| .NET 11 | ✅ | ❌ | ❌ |

## Developer information
**Integrate the library in own application**
1. Add the [LibreHardwareMonitorLib](https://www.nuget.org/packages/LibreHardwareMonitorLib/) NuGet package to your application.
2. Use the sample code below.

**Sample code**
```c#
Computer computer = new Computer
{
    IsCpuEnabled = true,
    IsGpuEnabled = true,
    IsMemoryEnabled = true,
    IsMotherboardEnabled = true,
    IsControllerEnabled = true,
    IsNetworkEnabled = true,
    IsStorageEnabled = true,
    IsPowerMonitorEnabled = true,
};

computer.Open();
computer.Accept(new UpdateVisitor());

foreach (IHardware hardware in computer.Hardware)
{
    Console.WriteLine("Hardware: {0}", hardware.Name);
    
    foreach (IHardware subhardware in hardware.SubHardware)
    {
        Console.WriteLine("\tSubhardware: {0}", subhardware.Name);
        
        foreach (ISensor sensor in subhardware.Sensors)
            Console.WriteLine("\t\tSensor: {0}, value: {1}", sensor.Name, sensor.Value);
    }

    foreach (ISensor sensor in hardware.Sensors)
        Console.WriteLine("\tSensor: {0}, value: {1}", sensor.Name, sensor.Value);
}

computer.Close();

public class UpdateVisitor : IVisitor
{
    public void VisitComputer(IComputer computer) => computer.Traverse(this);

    public void VisitHardware(IHardware hardware)
    {
        hardware.Update();
        foreach (IHardware subHardware in hardware.SubHardware)
            subHardware.Accept(this);
    }

    public void VisitSensor(ISensor sensor) { }

    public void VisitParameter(IParameter parameter) { }
}
```

**Administrator rights**

Some sensors require administrator privileges to access the data. Restart your IDE with admin privileges, or add an [app.manifest](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests) file to your project with requestedExecutionLevel on requireAdministrator.

## Warning

We're **not affiliated** with `librehardwaremonitor.com`.<br>
For your safety, please avoid using that site.

## Acknowledgements

Many thanks to all contributors listed below, and to JetBrains for providing tooling.

<a href="https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=LibreHardwareMonitor/LibreHardwareMonitor" />
</a>

## License
LibreHardwareMonitor is free and open source software licensed under MPL 2.0. Some parts of LibreHardwareMonitor are licensed under different terms, see [THIRD-PARTY-LICENSES](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/THIRD-PARTY-NOTICES.txt).
