# ksystemstats 系统监视器 GPU/磁盘/网络修复完整文档

## 一、问题概述

KDE Plasma 系统监视器（plasma-systemmonitor）在 **Qualcomm Adreno 平台（Android 容器 / droidspaces-gpu）** 上无法正确显示 GPU 占用、磁盘、网络数据。

- **GPU**：`gpu/gpu0/usage`、`gpu/all/usage` 传感器不存在，GPU 圆环空白
- **磁盘**：`disk/(?!all).*/used` 每设备传感器不存在，磁盘面板空白
- **网络**：`network/<接口>/download`、`upload` 等每设备传感器不存在，网络面板空白

## 二、根因分析

ksystemstats 是 KDE 系统监视器的数据守护进程（通过 D-Bus `org.kde.ksystemstats1` 提供传感器数据）。其内置插件只支持常见硬件，在容器/Adreno 环境下全部失效：

| 插件 | 依赖 | 失效原因 |
|------|------|---------|
| `ksystemstats_plugin_gpu.so` | udev 枚举 PCI DRM 设备，只识别 NVIDIA(`0x10de`)/AMD(`0x1002`)/Intel(`0x8086`) | Adreno 是 KGSL 平台设备，非 PCI DRM，vendor 不匹配 → 0 个 GPU → 空壳容器 |
| `ksystemstats_plugin_disk.so` | Solid + UDisks2 枚举存储卷 | 容器无 `/dev/block` 设备、无 udev → Solid 枚举 0 个卷 → 只有空聚合 |
| `ksystemstats_plugin_network.so` | libnl / NetworkManager | libnl 在容器中无法枚举/需要权限 → 0 个接口 |

**GPU 数据其实可用**，位于 sysfs：
| 文件 | 含义 | 示例 |
|------|------|------|
| `/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage` | GPU 占用率 | `21 %` |
| `/sys/class/kgsl/kgsl-3d0/gpuclk` | 当前频率（Hz） | `160000000` |
| `/sys/class/kgsl/kgsl-3d0/temp` | 温度（毫摄氏度） | `43200` |

**磁盘/网络数据也可用**，位于 `/proc`：
| 文件 | 用途 |
|------|------|
| `/proc/self/mounts` | 挂载点、文件系统类型 |
| `statvfs()` | 磁盘容量/已用/剩余 |
| `/proc/diskstats` | 磁盘读写速率 |
| `/sys/class/net` | 网络接口列表 |
| `/proc/net/dev` | 各接口收发字节/速率 |

## 三、解决方案

编写**两个自定义 ksystemstats 插件**，直接读取 sysfs/proc 数据并暴露为标准传感器，同时用 `dpkg-divert` 禁用官方空壳插件：

| 插件 | 提供传感器 | 数据来源 |
|------|-----------|---------|
| `ksystemstats_plugin_kgslgpu.so` | `gpu/gpu0/usage`、`gpu/all/usage`、`coreFrequency`、`temperature` | `/sys/class/kgsl/kgsl-3d0/` |
| `ksystemstats_plugin_containerio.so` | `disk/loop55`、`disk/all` 全套 | `/proc/self/mounts` + `statvfs()` + `/proc/diskstats` |
| | `network/rmnet_ipa0`、`ds-br0`、`wlan0` 等 + `network/all` | `/sys/class/net` + `/proc/net/dev` + `QNetworkInterface` |

## 四、完整修复过程

### 4.1 安装编译工具链

需要 Qt6 头文件 + KPluginFactory 头文件（编译器 g++ 系统已自带）：

```bash
echo '1234' | sudo -S apt-get install -y libkf6coreaddons-dev
```

> 安装了 17 个包（qt6-base-dev、Qt6/X11 开发头文件、KCoreAddons 开发文件）。

### 4.2 Vendor libksysguard 系统头文件

ksystemstats 插件需要 `libksysguardsystemstats2` 的头文件（`SensorPlugin.h` 等）。开发包 `libksysguard-dev` 会拉 69 个包，太重。改为从匹配的版本（6.6.5）下载头文件，同时提供截断的 `systemstats_export.h`、`formatter/Unit.h`、`formatter_export.h`。

```bash
mkdir -p /home/Gold/gpu-monitor-fix/{systemstats,formatter}
cd /home/Gold/gpu-monitor-fix/systemstats
for f in SensorPlugin.h SensorContainer.h SensorObject.h SensorProperty.h SysFsSensor.h SensorInfo.h AggregateSensor.h; do
  curl -fsSL "https://raw.githubusercontent.com/KDE/libksysguard/v6.6.5/systemstats/$f" -o "$f"
done
```

> 关键：`SensorObject` 构造函数通过 `Qt::QueuedConnection` 异步把对象加入容器，所以测试时必须调用 `QCoreApplication::processEvents()` 才能看到对象。

### 4.3 编写 GPU 插件 `kgslgpuplugin.cpp`

```cpp
#include <KPluginFactory>
#include <QFile>
#include <systemstats/AggregateSensor.h>
#include <systemstats/SensorContainer.h>
#include <systemstats/SensorObject.h>
#include <systemstats/SensorPlugin.h>
#include <systemstats/SensorProperty.h>
#include <systemstats/SysFsSensor.h>

using namespace KSysGuard;
const QString kgslPath = QStringLiteral("/sys/class/kgsl/kgsl-3d0/");

class KgslGpuPlugin : public SensorPlugin {
    Q_OBJECT
public:
    KgslGpuPlugin(QObject *parent, const QVariantList &args) : SensorPlugin(parent, args) {
        m_container = new SensorContainer(QStringLiteral("gpu"), QStringLiteral("GPU"), this);
        m_gpu0 = new SensorObject(QStringLiteral("gpu0"), QStringLiteral("GPU"), m_container);
        m_container->addObject(m_gpu0);

        auto *nameP = new SensorProperty(QStringLiteral("name"), m_gpu0);
        nameP->setValue(readString("gpu_model"));

        m_usage = new SysFsSensor(QStringLiteral("usage"), kgslPath + "gpu_busy_percentage", m_gpu0);
        m_usage->setUnit(UnitPercent); m_usage->setMax(100);
        m_usage->setVariantType(QVariant::Double);
        m_usage->setConvertFunction([](const QByteArray &in) {
            QString s = QString::fromLatin1(in).trimmed(); s.remove('%');
            return QVariant(s.toDouble());
        });

        m_frequency = new SysFsSensor(QStringLiteral("coreFrequency"), kgslPath + "gpuclk", m_gpu0);
        m_frequency->setUnit(UnitMegaHertz); m_frequency->setVariantType(QVariant::Double);
        m_frequency->setConvertFunction([](const QByteArray &in) {
            return QVariant(QString::fromLatin1(in).trimmed().toDouble() / 1e6);
        });

        m_temperature = new SysFsSensor(QStringLiteral("temperature"), kgslPath + "temp", m_gpu0);
        m_temperature->setUnit(UnitCelsius); m_temperature->setVariantType(QVariant::Double);
        m_temperature->setConvertFunction([](const QByteArray &in) {
            return QVariant(QString::fromLatin1(in).trimmed().toDouble() / 1000.0);
        });

        // gpu/all 聚合
        m_all = new SensorObject(QStringLiteral("all"), QStringLiteral("All GPUs"), m_container);
        m_container->addObject(m_all);
        auto *allUsage = new AggregateSensor(m_all, QStringLiteral("usage"), QStringLiteral("All GPUs Usage"));
        allUsage->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("usage"));
        allUsage->setAggregateFunction([](const QVariant &a, const QVariant &b) {
            return QVariant::fromValue(a.toDouble() + b.toDouble());
        });
        allUsage->setUnit(UnitPercent); allUsage->setMax(100);
    }
    void update() override { m_usage->update(); m_frequency->update(); m_temperature->update(); }
    QString providerName() const override { return QStringLiteral("kgslgpu"); }
private:
    SensorContainer *m_container; SensorObject *m_gpu0, *m_all;
    SysFsSensor *m_usage, *m_frequency, *m_temperature;
};
K_PLUGIN_CLASS_WITH_JSON(KgslGpuPlugin, "metadata.json")
#include "kgslgpuplugin.moc"
```

`metadata.json`：
```json
{ "providerName": "kgslgpu" }
```

> **重要经验**：`SensorPlugin::providerName()` 默认返回空字符串。若两个插件都返回空，daemon 会因 `registerProvider` 去重而拒绝后加载的插件。**必须覆写 `providerName()` 返回唯一字符串**，否则插件加载后容器为空。

### 4.4 编写磁盘+网络插件 `container/containerplugin.cpp`

核心逻辑：
- **磁盘**：解析 `/proc/self/mounts`，过滤真实文件系统（ext4/f2fs/xfs 等）且挂载点不以 `/run` 开头；`update()` 中 `statvfs()` 取容量，`/proc/diskstats` 按设备名匹配算读写速率。
- **网络**：枚举 `/sys/class/net`，过滤掉 lo 和纯虚拟/隧道接口（只有 IP 或 wlan/eth/rmnet 等真实接口才保留）；`update()` 中 `/proc/net/dev` 算速率（两次采样差值 / 间隔），`QNetworkInterface` 取 IPv4。
- 每个接口提供：
  - `network`（接口名）、`download`/`upload`（B/s）、`downloadBits`/`uploadBits`（bit/s）
  - `totalDownload`/`totalUpload`（累计字节）
  - `ipv4address`（仅 IPv4，不显示 IPv6）
  - `rate`（格式化字符串 `rx:xxx tx:xxx`）、`total`（格式化字符串 `dl:xxx ul:xxx`）
  - `line`（合并显示 `IP  rx:xxx tx:xxx  dl:xxx ul:xxx`，用于系统概览页）
- 提供 `disk/all`、`network/all` 聚合。

**关键细节**：
- 用 `QFile::readAll()` + `split('\n')` 解析 proc 文件，避免容器内 `QFile::readLine()` 依赖 `size()` 判断 EOF 的坑。
- `PercentageSensor` 依赖基传感器的 `max`，必须在 `update()` 里同步设置 `used`/`free` 的 `max = total`，否则百分比为 0。
- 速率计算：每次 `update()` 记录时间戳和字节数，下一次用 `(当前 - 上次) / 间隔` 算速率；首次调用只打基线。
- `QDir::entryList(QDir::Dirs)` 在 sysfs 上可能不返回目录（容器环境限制），改用 `for (int i = 0; i < 100; ++i)` 显式遍历 thermal_zoneN。

### 4.4.1 CPU 温度传感器

ksystemstats 官方 CPU 插件依赖 lm-sensors 获取温度，但在 Adreno 平台上不可用（返回 0）。改为从 sysfs thermal zone 直接读取：

```cpp
// 扫描 thermal_zone0~99，找到 type 以 "cpu-" 开头的第一个
for (int i = 0; i < 100; ++i) {
    QString zonePath = QStringLiteral("/sys/class/thermal/thermal_zone%1").arg(i);
    QFile typeFile(zonePath + "/type");
    if (!typeFile.open(QIODevice::ReadOnly)) continue;
    QString type = QString::fromUtf8(typeFile.readAll()).trimmed();
    if (type.startsWith("cpu-")) {
        m_cpuTempPath = zonePath + "/temp";
        break;
    }
}
```

使用 `SysFsSensor` 读取并转换（毫摄氏度 → 摄氏度）：
```cpp
m_cpuTemp = new SysFsSensor("temperature", m_cpuTempPath, m_cpuObj);
m_cpuTemp->setConvertFunction([](const QByteArray &in) {
    return QVariant(QString::fromLatin1(in).trimmed().toDouble() / 1000.0);
});
```

传感器路径：`thermal/cpu/temperature`。

### 4.5 编译

> **注意**：源码末尾 `#include "xxx.moc"` 对应的是 `xxx.moc`（无 `.cpp` 前缀）。`build.sh` 里 moc 输出文件名必须用 `${src%.cpp}.moc`，否则编译报 `xxx.moc: 没有那个文件`。

```bash
# GPU 插件
/usr/lib/qt6/libexec/moc -I. -I/usr/include/aarch64-linux-gnu/qt6 -I/usr/include/aarch64-linux-gnu/qt6/QtCore -I/usr/include/aarch64-linux-gnu/qt6/QtDBus -I/usr/include/KF6/KCoreAddons kgslgpuplugin.cpp -o kgslgpuplugin.moc
g++ -std=c++17 -fPIC -Wno-deprecated-declarations -I. -I/usr/include/aarch64-linux-gnu/qt6 -I/usr/include/aarch64-linux-gnu/qt6/QtCore -I/usr/include/aarch64-linux-gnu/qt6/QtDBus -I/usr/include/KF6/KCoreAddons -c kgslgpuplugin.cpp -o kgslgpuplugin.o
g++ -fPIC -shared -o ksystemstats_plugin_kgslgpu.so kgslgpuplugin.o -Wl,-rpath-link,/usr/lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu/libKSysGuardSystemStats.so.6.6.5 -lKF6CoreAddons -lQt6DBus -lQt6Core

# 磁盘+网络插件（多 Qt6Network）
cd container
/usr/lib/qt6/libexec/moc -I. -I.. -I/usr/include/aarch64-linux-gnu/qt6 -I/usr/include/aarch64-linux-gnu/qt6/QtCore -I/usr/include/aarch64-linux-gnu/qt6/QtDBus -I/usr/include/aarch64-linux-gnu/qt6/QtNetwork -I/usr/include/KF6/KCoreAddons containerplugin.cpp -o containerplugin.moc
g++ -std=c++17 -fPIC -Wno-deprecated-declarations -I. -I.. -I/usr/include/aarch64-linux-gnu/qt6 -I/usr/include/aarch64-linux-gnu/qt6/QtCore -I/usr/include/aarch64-linux-gnu/qt6/QtDBus -I/usr/include/aarch64-linux-gnu/qt6/QtNetwork -I/usr/include/KF6/KCoreAddons -c containerplugin.cpp -o containerplugin.o
g++ -fPIC -shared -o ksystemstats_plugin_containerio.so containerplugin.o -Wl,-rpath-link,/usr/lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu/libKSysGuardSystemStats.so.6.6.5 -lKF6CoreAddons -lQt6Network -lQt6DBus -lQt6Core
```

> 链接时直接指定 `libKSysGuardSystemStats.so.6.6.5` 全路径（无 dev 符号链接），其 SONAME 是 `libKSysGuardSystemStats.so.2`，链接器会自动记录正确的 DT_NEEDED。

### 4.6 安装并禁用官方空壳插件

```bash
PLUG=/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats

# 禁用官方 GPU/磁盘/网络插件（dpkg-divert 重命名，不影响系统升级）
echo '1234' | sudo -S dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_gpu.so
echo '1234' | sudo -S dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_disk.so
echo '1234' | sudo -S dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_network.so

# 安装自定义插件
echo '1234' | sudo -S cp /home/Gold/gpu-monitor-fix/ksystemstats_plugin_kgslgpu.so   $PLUG/
echo '1234' | sudo -S cp /home/Gold/gpu-monitor-fix/container/ksystemstats_plugin_containerio.so $PLUG/
echo '1234' | sudo -S chmod 644 $PLUG/ksystemstats_plugin_*.so

# 重启
killall -9 ksystemstats
# 或
systemctl --user restart plasma-ksystemstats.service
```

> `dpkg-divert --rename` 会把原文件改名为 `xxx.so.distrib`，包管理器升级时也会把新包文件放到 `.distrib`，不会覆盖我们的自定义插件。

一键安装脚本（包含以上步骤 + 概览页配置更新）：
```bash
bash /home/Gold/gpu-monitor-fix/install.sh
```

### 4.7 修复概览页网络面板

#### 问题：GroupedText 死锁

概览页网络面板使用 `org.kde.ksysguard.textonly`（GroupedText）。当 `groupByTotal=true` 时，GroupedText 内部用 `Sensors.Sensor` 组件订阅 `totalSensors` 中的传感器 ID。

```qml
// GroupedText.qml
Sensors.Sensor {
    id: groupSensor
    sensorId: root.hasGroups ? modelData : ""  // modelData = "network/(?!all).*/network"
}
```

`Sensors.Sensor` 的 `sensorId` 是正则表达式时，会匹配多个传感器。每次 `value` 变化触发 `model` 重新计算，`model` 重新计算又触发新的订阅/取消订阅 → **QML 引擎死循环，系统监视器卡死**。

#### 修复：改用 `line` 合并传感器

不在 `totalSensors` 中传正则，改用每个接口的 `line` 合并传感器，`groupByTotal=false` 平铺显示：

```ini
[Face-101427258480848][Appearance]
Title=网络
chartFace=org.kde.ksysguard.textonly

[Face-101427258480848][Sensors]
highPrioritySensorIds=["network/(?!all).*/line"]
totalSensors=[]

[Face-101427258480848][org.kde.ksysguard.textonly][General]
groupByTotal=false
```

每个接口显示一行：
```
rmnet_ipa0  fe80::...  rx:0 B/s tx:0 B/s  dl:402.6 MB ul:1.06 GB
wlan0       192.168.1.1  rx:0 B/s tx:0 B/s  dl:0 B ul:0 B
```

#### CPU/GPU 面板显示优化

CPU 和 GPU 面板改为 `org.kde.ksysguard.textonly`，显示使用率、频率、温度三个指标：

```ini
[Face-106123380916688][Appearance]
Title=CPU
chartFace=org.kde.ksysguard.textonly

[Face-106123380916688][Sensors]
highPrioritySensorIds=["cpu/all/usage","cpu/all/averageFrequency","thermal/cpu/temperature"]
totalSensors=["cpu/all/name"]
```

```ini
[Face-106123406501568][Appearance]
Title=GPU
chartFace=org.kde.ksysguard.textonly

[Face-106123406501568][Sensors]
highPrioritySensorIds=["gpu/gpu0/usage","gpu/gpu0/coreFrequency","gpu/gpu0/temperature"]
totalSensors=["gpu/gpu0/name"]
```

### 4.8 验证

通过 D-Bus 查询传感器是否出现、数值是否正常：

```bash
busctl --user call org.kde.ksystemstats1 /org/kde/ksystemstats1 org.kde.ksystemstats1 allSensors | grep -oE '"(gpu|disk|network|thermal)/[^"]*"' | sort -u
```

或 Python 订阅并读取：

```python
import dbus, time
bus = dbus.SessionBus()
svc = bus.get_object('org.kde.ksystemstats1', '/org/kde/ksystemstats1')
iface = dbus.Interface(svc, 'org.kde.ksystemstats1')
ids = ['gpu/gpu0/usage', 'gpu/gpu0/coreFrequency', 'gpu/gpu0/temperature',
       'cpu/all/usage', 'cpu/all/averageFrequency', 'thermal/cpu/temperature',
       'disk/loop55/usedPercent', 'network/rmnet_ipa0/line']
iface.subscribe(ids)
time.sleep(3)
data = dict(iface.sensorData(ids))
for k, v in sorted(data.items()):
    print(f'{k}: {v}')
```

## 五、修复结果

| 项目 | 修复前 | 修复后 |
|------|--------|--------|
| GPU 占用率 | 空壳（无子传感器） | 实时 %（如 21%） |
| GPU 频率 | 无 | 160 MHz |
| GPU 温度 | 无 | 44°C |
| CPU 使用率 | 正常 | 正常 |
| CPU 频率 | 正常 | 正常 |
| CPU 温度 | 0（lm-sensors 不可用） | 61°C（从 thermal zone 读取） |
| 磁盘已用 | 空白 | 37.5%（32GB 根分区） |
| 磁盘读写速率 | 无 | 实时 B/s |
| 网络下载/上传 | 空白 | 实时速率 + 累计流量 |
| 各接口 IP | 无 | IPv4 正确显示 |

## 六、新安装如何修复

### 6.1 准备源码（本机已有）

源码在 `/home/Gold/gpu-monitor-fix/`：
```
gpu-monitor-fix/
├── build.sh                 # 一键编译+安装脚本
├── kgslgpuplugin.cpp        # GPU 插件源码
├── metadata.json            # GPU 插件元数据
├── systemstats/             # vendored 头文件
├── formatter/               # vendored 头文件
└── container/
    ├── containerplugin.cpp  # 磁盘+网络插件源码
    └── metadata.json
```

### 6.2 新机器安装（预编译产物）

新系统上直接用 `install.sh` 一键安装预编译产物（无需编译）：

```bash
# 1. 拷贝源码目录到新系统，运行安装脚本
bash /home/Gold/gpu-monitor-fix/install.sh
```

`install.sh` 会自动：禁用官方插件 → 安装自定义插件 → 更新概览页配置 → 重启 ksystemstats。

### 6.3 新机器编译安装（需重新编译时）

```bash
# 1. 安装编译工具链
sudo apt-get install -y libkf6coreaddons-dev

# 2. 禁用官方插件
PLUG=/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats
sudo dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_gpu.so
sudo dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_disk.so
sudo dpkg-divert --rename --local --add $PLUG/ksystemstats_plugin_network.so

# 3. 编译并安装
cd /home/Gold/gpu-monitor-fix && bash build.sh
```

`build.sh` 会自动：重新生成 moc → 编译两个插件 → 用 sudo 安装到插件目录 → 重启 ksystemstats。

### 6.4 手动修改概览页

如果 `install.sh` 中的概览页更新失败，或系统监视器覆盖了配置，手动修改 `~/.local/share/plasma-systemmonitor/overview.page`：

**网络面板**（改为 `line` 合并传感器，避免 GroupedText 死锁）：
```ini
[Face-101427258480848][Appearance]
Title=网络
chartFace=org.kde.ksysguard.textonly

[Face-101427258480848][Sensors]
highPrioritySensorIds=["network/(?!all).*/line"]
totalSensors=[]

[Face-101427258480848][org.kde.ksysguard.textonly][General]
groupByTotal=false
```

**CPU 面板**（改为 textonly，显示使用率+频率+温度）：
```ini
[Face-106123380916688][Appearance]
Title=CPU
chartFace=org.kde.ksysguard.textonly

[Face-106123380916688][Sensors]
highPrioritySensorIds=["cpu/all/usage","cpu/all/averageFrequency","thermal/cpu/temperature"]
totalSensors=["cpu/all/name"]
```

**GPU 面板**（改为 textonly，显示使用率+频率+温度）：
```ini
[Face-106123406501568][Appearance]
Title=GPU
chartFace=org.kde.ksysguard.textonly

[Face-106123406501568][Sensors]
highPrioritySensorIds=["gpu/gpu0/usage","gpu/gpu0/coreFrequency","gpu/gpu0/temperature"]
totalSensors=["gpu/gpu0/name"]
```

### 6.5 版本兼容性

- 插件编译时链接的 `libKSysGuardSystemStats.so.6.6.5` 需与目标系统版本**一致**（或其 ABI 兼容）。若系统 ksystemstats 版本不同，需要把 `build.sh` 里的 `SYSSTATS_LIB` 路径改为对应版本，并确认 vendored 头文件与版本匹配。
- 若系统是 Debian/Kubuntu 而非 Ubuntu 26.04，插件目录路径可能不同（`/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats/`），需按实际 `kf6-config --path plugin` 查询。

## 七、推送到上游 GitHub

### 7.1 准备工作

1. **注册 GitHub 账号**，访问 https://github.com 创建仓库（如 `kde-ksystemstats-adreno/`）。
2. **安装 git 并配置**：
   ```bash
   sudo apt-get install -y git
   git config --global user.name "你的名字"
   git config --global user.email "你的邮箱"
   ```
3. **生成 SSH 密钥并添加到 GitHub**（设置 → SSH and GPG keys）：
   ```bash
   ssh-keygen -t ed25519 -C "你的邮箱"
   cat ~/.ssh/id_ed25519.pub   # 复制公钥到 GitHub
   ```

### 7.2 创建本地仓库并提交

```bash
cd /home/Gold/gpu-monitor-fix
git init
# 编写 README 说明用途
# 忽略编译产物
cat > .gitignore <<'EOF'
*.o
*.moc
*.so
EOF

git add build.sh install.sh kgslgpuplugin.cpp metadata.json container/ systemstats/ formatter/ README.md
git commit -m "Add ksystemstats plugins for Qualcomm Adreno GPU/disk/network monitoring"

# 关联远程仓库
git remote add origin git@github.com:用户名/repo名.git
git branch -M main
git push -u origin main
```

### 7.3 提交到 KDE 上游（建议）

真正为社区做贡献应提交到 KDE 官方的 ksystemstats 仓库（`https://invent.kde.org/plasma/ksystemstats`）。流程：

1. **注册 KDE identity**：https://identity.kde.org
2. **提交补丁**（推荐方式，无需立刻有提交权限）：
   - 在 https://bugs.kde.org 搜索是否已有相关 bug（如 GPU 支持、容器支持）
   - 若无，新建 bug 报告，附上补丁
   - 或在 invent.kde.org 上 fork 仓库，修改后发起 Merge Request

3. **建议的补丁内容**（把插件逻辑合并进官方代码）：
   - 在 `plugins/gpu/` 新增 `LinuxKgslGpu.{h,cpp}`，在 `LinuxBackend.cpp` 的 udev 枚举后检测 `/sys/class/kgsl/kgsl-3d0/` 是否存在，存在则创建对应的 GPU 设备
   - 在 `plugins/disks/` 增加 Solid 枚举为空时的 `/proc/mounts` + `statvfs()` fallback
   - 在 `plugins/network/` 增加 libnl 失败时的 `/proc/net/dev` fallback
   - 更新各 `CMakeLists.txt` 把新源文件加入构建

> 注意：官方代码风格用 `i18n()`/`i18nc()` 处理翻译，用 `K_PLUGIN_CLASS_WITH_JSON`。提交前先阅读 ksystemstats 的 `CONTRIBUTING` 或维护者约定，并确保通过 `cmake` 完整构建。

### 7.4 提交规范

- 提交信息用英文，遵循上游风格（如 `plugins/gpu: add KGSL (Adreno) backend`）
- 每个逻辑改动一个提交
- 提供清晰的 `git diff` 和说明

## 八、文件清单

```
/home/Gold/gpu-monitor-fix/
├── install.sh                  # 一键安装脚本（预编译产物安装 + 概览页配置）
├── build.sh                    # 一键编译安装脚本（需编译工具链）
├── kgslgpuplugin.cpp           # GPU (Adreno/KGSL) 插件
├── ksystemstats_plugin_kgslgpu.so  # GPU 插件预编译产物
├── metadata.json               # GPU 插件元数据 (providerName: kgslgpu)
├── systemstats/                # libksysguard 头文件 (v6.6.5)
│   ├── SensorPlugin.h
│   ├── SensorContainer.h
│   ├── SensorObject.h
│   ├── SensorProperty.h
│   ├── SysFsSensor.h
│   ├── SensorInfo.h
│   ├── AggregateSensor.h
│   └── systemstats_export.h
├── formatter/
│   ├── Unit.h
│   └── formatter_export.h
└── container/
    ├── containerplugin.cpp     # 磁盘+网络+CPU温度插件
    ├── ksystemstats_plugin_containerio.so  # 容器插件预编译产物
    └── metadata.json           # 插件元数据 (providerName: containerio)
```

已安装插件：
```
/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats/ksystemstats_plugin_kgslgpu.so
/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats/ksystemstats_plugin_containerio.so
```

已禁用官方插件（dpkg-divert）：
```
ksystemstats_plugin_gpu.so.distrib
ksystemstats_plugin_disk.so.distrib
ksystemstats_plugin_network.so.distrib
```

## 九、遗留事项

1. **GroupedText 死锁**：`groupByTotal=true` + `totalSensors` 正则会导致系统监视器卡死（`Sensors.Sensor` 订阅正则死循环）。当前网络面板用 `line` 合并传感器 + `groupByTotal=false` 规避。若 KDE 上游修复该 QML 组件，可恢复多列分组显示。
2. **网络接口过滤**：`containerplugin.cpp` 的 `isUsableInterface()` 只保留有 IP 或真实接口。若用户想显示更多/更少接口，可调整此函数。
3. **磁盘 I/O 速率**：首次显示为 0（需两次采样），之后正常。
4. **CPU 温度**：从第一个 `cpu-*` thermal zone 读取，若系统无此 zone 会回退到 `thermal_zone5`。
5. **上游修复**：若 ksystemstats 上游未来原生支持 Adreno/容器环境，可移除自定义插件，恢复官方插件。
6. **sudo 密码**：本文档示例使用 `1234`，实际请勿在明文文档中保留，建议尽快 `passwd` 更换。