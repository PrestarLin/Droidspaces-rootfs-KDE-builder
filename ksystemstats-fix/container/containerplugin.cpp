/*
 * ksystemstats plugin that provides disk and network sensors using /proc data.
 * Works in containers where Solid/udev/NetworkManager are unavailable.
 *
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KPluginFactory>

#include <QDir>
#include <QFile>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <sys/statvfs.h>

#include <systemstats/AggregateSensor.h>
#include <systemstats/SensorContainer.h>
#include <systemstats/SensorObject.h>
#include <systemstats/SensorPlugin.h>
#include <systemstats/SensorProperty.h>
#include <systemstats/SysFsSensor.h>

using namespace KSysGuard;

// ============================================================================
// Disk sensors
// ============================================================================

static const QSet<QString> realFs = {QStringLiteral("ext2"), QStringLiteral("ext3"), QStringLiteral("ext4"),
    QStringLiteral("btrfs"), QStringLiteral("xfs"), QStringLiteral("f2fs"), QStringLiteral("reiserfs")};

class DiskVolume : public SensorObject
{
    Q_OBJECT
public:
    DiskVolume(const QString &deviceId, const QString &mountPoint, const QString &fsType, SensorContainer *parent)
        : SensorObject(deviceId, mountPoint, parent)
        , m_deviceId(deviceId)
        , m_mountPoint(mountPoint)
    {
        new SensorProperty(QStringLiteral("name"), mountPoint, this);
        m_total = new SensorProperty(QStringLiteral("total"), QStringLiteral("Total Space"), QVariant(0.0), this);
        m_total->setUnit(UnitByte);
        m_total->setVariantType(QVariant::ULongLong);
        m_used = new SensorProperty(QStringLiteral("used"), QStringLiteral("Used Space"), QVariant(0.0), this);
        m_used->setUnit(UnitByte);
        m_used->setVariantType(QVariant::ULongLong);
        m_free = new SensorProperty(QStringLiteral("free"), QStringLiteral("Free Space"), QVariant(0.0), this);
        m_free->setUnit(UnitByte);
        m_free->setVariantType(QVariant::ULongLong);

        auto *usedPercent = new PercentageSensor(this, QStringLiteral("usedPercent"), QStringLiteral("Percentage Used"));
        usedPercent->setBaseSensor(m_used);
        auto *freePercent = new PercentageSensor(this, QStringLiteral("freePercent"), QStringLiteral("Percentage Free"));
        freePercent->setBaseSensor(m_free);

        m_read = new SensorProperty(QStringLiteral("read"), QStringLiteral("Read Rate"), QVariant(0.0), this);
        m_read->setUnit(UnitByteRate);
        m_read->setVariantType(QVariant::Double);
        m_write = new SensorProperty(QStringLiteral("write"), QStringLiteral("Write Rate"), QVariant(0.0), this);
        m_write->setUnit(UnitByteRate);
        m_write->setVariantType(QVariant::Double);
    }

    void updateDiskUsage()
    {
        struct statvfs buf;
        if (::statvfs(m_mountPoint.toLocal8Bit().constData(), &buf) != 0 || buf.f_blocks == 0) {
            return;
        }
        qulonglong total = static_cast<qulonglong>(buf.f_blocks) * buf.f_frsize;
        qulonglong free = static_cast<qulonglong>(buf.f_bfree) * buf.f_frsize;
        m_total->setValue(total);
        m_total->setMax(total);
        m_used->setMax(total);
        m_free->setMax(total);
        m_free->setValue(free);
        m_used->setValue(total - free);
    }

    void setIORates(double readRate, double writeRate)
    {
        m_read->setValue(readRate);
        m_write->setValue(writeRate);
    }

    QString deviceId() const { return m_deviceId; }

private:
    QString m_deviceId;
    QString m_mountPoint;
    SensorProperty *m_total = nullptr;
    SensorProperty *m_used = nullptr;
    SensorProperty *m_free = nullptr;
    SensorProperty *m_read = nullptr;
    SensorProperty *m_write = nullptr;
};

// ============================================================================
// Network sensors
// ============================================================================

class NetInterface : public SensorObject
{
    Q_OBJECT
public:
    NetInterface(const QString &ifname, SensorContainer *parent)
        : SensorObject(ifname, ifname, parent)
        , m_ifname(ifname)
    {
        new SensorProperty(QStringLiteral("name"), ifname, this);
        auto *netSensor = new SensorProperty(QStringLiteral("network"), ifname, this);
        netSensor->setVariantType(QVariant::String);
        netSensor->setValue(ifname);
        // Set name sensor value
        sensor(QStringLiteral("name"))->setValue(ifname);
        m_download = new SensorProperty(QStringLiteral("download"), QStringLiteral("Download Rate"), QVariant(0.0), this);
        m_download->setUnit(UnitByteRate);
        m_download->setVariantType(QVariant::Double);
        m_upload = new SensorProperty(QStringLiteral("upload"), QStringLiteral("Upload Rate"), QVariant(0.0), this);
        m_upload->setUnit(UnitByteRate);
        m_upload->setVariantType(QVariant::Double);
        m_downloadBits = new SensorProperty(QStringLiteral("downloadBits"), QStringLiteral("Download Rate"), QVariant(0.0), this);
        m_downloadBits->setUnit(UnitBitRate);
        m_downloadBits->setVariantType(QVariant::Double);
        m_uploadBits = new SensorProperty(QStringLiteral("uploadBits"), QStringLiteral("Upload Rate"), QVariant(0.0), this);
        m_uploadBits->setUnit(UnitBitRate);
        m_uploadBits->setVariantType(QVariant::Double);
        m_totalDownload = new SensorProperty(QStringLiteral("totalDownload"), QStringLiteral("Total Downloaded"), QVariant(0.0), this);
        m_totalDownload->setUnit(UnitByte);
        m_totalDownload->setVariantType(QVariant::Double);
        m_totalUpload = new SensorProperty(QStringLiteral("totalUpload"), QStringLiteral("Total Uploaded"), QVariant(0.0), this);
        m_totalUpload->setUnit(UnitByte);
        m_totalUpload->setVariantType(QVariant::Double);
        m_ipv4 = new SensorProperty(QStringLiteral("ipv4address"), this);
        m_ipv4->setName(ifname + QStringLiteral(" IP"));
        m_ipv4->setVariantType(QVariant::String);
        m_ipv6 = new SensorProperty(QStringLiteral("ipv6address"), this);
        m_ipv6->setName(ifname + QStringLiteral(" IPv6"));
        m_ipv6->setVariantType(QVariant::String);

        m_rate = new SensorProperty(QStringLiteral("rate"), QStringLiteral("Rate"), QVariant(QString()), this);
        m_rate->setName(ifname + QStringLiteral(" Rate"));
        m_rate->setVariantType(QVariant::String);
        m_total = new SensorProperty(QStringLiteral("total"), QStringLiteral("Total"), QVariant(QString()), this);
        m_total->setName(ifname + QStringLiteral(" Total"));
        m_total->setVariantType(QVariant::String);
        m_line = new SensorProperty(QStringLiteral("line"), QStringLiteral("Line"), QVariant(QString()), this);
        m_line->setName(ifname);
        m_line->setVariantType(QVariant::String);

        updateAddresses();
    }

    void updateAddresses()
    {
        const auto iface = QNetworkInterface::interfaceFromName(m_ifname);
        QStringList v4, v6;
        for (const auto &entry : iface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                v4 << ip.toString();
            } else if (ip.protocol() == QAbstractSocket::IPv6Protocol) {
                v6 << ip.toString();
            }
        }
        m_ipv4->setValue(v4.join(QStringLiteral(", ")));
        m_ipv6->setValue(v6.join(QStringLiteral(", ")));
    }

    void setTraffic(double rxBytes, double txBytes)
    {
        m_download->setValue(rxBytes);
        m_upload->setValue(txBytes);
        m_downloadBits->setValue(rxBytes * 8);
        m_uploadBits->setValue(txBytes * 8);
        m_rate->setValue(formatRate(rxBytes, txBytes));
        updateLine();
    }

    void setTotals(double rxTotal, double txTotal)
    {
        m_totalDownload->setValue(rxTotal);
        m_totalUpload->setValue(txTotal);
        m_total->setValue(formatTotal(rxTotal, txTotal));
        updateLine();
    }

    void updateLine()
    {
        QString ip = m_ipv4->value().toString();
        QString rt = m_rate->value().toString();
        QString tl = m_total->value().toString();
        m_line->setValue(ip + QStringLiteral("  ") + rt + QStringLiteral("  ") + tl);
    }

    QString ifname() const { return m_ifname; }

    static QString formatRate(double rx, double tx)
    {
        return QStringLiteral("rx:%1 tx:%2").arg(formatBytesPerSec(rx), formatBytesPerSec(tx));
    }

    static QString formatTotal(double rx, double tx)
    {
        return QStringLiteral("dl:%1 ul:%2").arg(formatBytes(rx), formatBytes(tx));
    }

    static QString formatBytesPerSec(double bps)
    {
        if (bps >= 1e9) return QString::number(bps / 1e9, 'f', 1) + QStringLiteral(" GB/s");
        if (bps >= 1e6) return QString::number(bps / 1e6, 'f', 1) + QStringLiteral(" MB/s");
        if (bps >= 1e3) return QString::number(bps / 1e3, 'f', 1) + QStringLiteral(" KB/s");
        return QString::number(bps, 'f', 0) + QStringLiteral(" B/s");
    }

    static QString formatBytes(double bytes)
    {
        if (bytes >= 1e12) return QString::number(bytes / 1e12, 'f', 2) + QStringLiteral(" TB");
        if (bytes >= 1e9) return QString::number(bytes / 1e9, 'f', 2) + QStringLiteral(" GB");
        if (bytes >= 1e6) return QString::number(bytes / 1e6, 'f', 1) + QStringLiteral(" MB");
        if (bytes >= 1e3) return QString::number(bytes / 1e3, 'f', 0) + QStringLiteral(" KB");
        return QString::number(bytes, 'f', 0) + QStringLiteral(" B");
    }

private:
    QString m_ifname;
    SensorProperty *m_download = nullptr;
    SensorProperty *m_upload = nullptr;
    SensorProperty *m_downloadBits = nullptr;
    SensorProperty *m_uploadBits = nullptr;
    SensorProperty *m_totalDownload = nullptr;
    SensorProperty *m_totalUpload = nullptr;
    SensorProperty *m_ipv4 = nullptr;
    SensorProperty *m_ipv6 = nullptr;
    SensorProperty *m_rate = nullptr;
    SensorProperty *m_total = nullptr;
    SensorProperty *m_line = nullptr;
};

// ============================================================================
// ContainerPlugin
// ============================================================================

class ContainerPlugin : public SensorPlugin
{
    Q_OBJECT
public:
    ContainerPlugin(QObject *parent, const QVariantList &args)
        : SensorPlugin(parent, args)
    {
        initDisk();
        initNetwork();
        initCpuTemp();
    }

    void update() override
    {
        updateDisk();
        updateNetwork();
        updateCpuTemp();
    }

    QString providerName() const override
    {
        return QStringLiteral("containerio");
    }

private:
    void initDisk()
    {
        m_diskContainer = new SensorContainer(QStringLiteral("disk"), QStringLiteral("Disks"), this);
        m_diskVolumes.clear();

        QFile mounts(QStringLiteral("/proc/self/mounts"));
        if (!mounts.open(QIODevice::ReadOnly)) {
            return;
        }
        const auto lines = mounts.readAll().split('\n');
        for (const auto &raw : lines) {
            if (raw.isEmpty()) continue;
            const auto parts = raw.split(' ');
            if (parts.size() < 6) continue;
            const QString device = QString::fromUtf8(parts[0]);
            const QString mountPoint = QString::fromUtf8(parts[1]);
            const QString fsType = QString::fromUtf8(parts[2]);
            if (!realFs.contains(fsType)) continue;
            if (!device.startsWith(QStringLiteral("/dev/"))) continue;
            if (mountPoint.startsWith(QStringLiteral("/run"))) continue;
            m_diskVolumes.append(new DiskVolume(QFileInfo(device).fileName(), mountPoint, fsType, m_diskContainer));
        }
        addDiskAggregates();
    }

    void addDiskAggregates()
    {
        auto all = new SensorObject(QStringLiteral("all"), QStringLiteral("All Disks"), m_diskContainer);
        auto filter = [](const SensorProperty *s) {
            return s->parentObject()->id() != QStringLiteral("all");
        };

        auto *total = new AggregateSensor(all, QStringLiteral("total"), QStringLiteral("Total Space"));
        total->setUnit(UnitByte);
        total->setVariantType(QVariant::ULongLong);
        total->setMatchSensors(QRegularExpression(QStringLiteral("^.*$")), QStringLiteral("total"));
        total->setFilterFunction(filter);

        auto *free = new AggregateSensor(all, QStringLiteral("free"), QStringLiteral("Free Space"));
        free->setUnit(UnitByte);
        free->setVariantType(QVariant::ULongLong);
        free->setMatchSensors(QRegularExpression(QStringLiteral("^.*$")), QStringLiteral("free"));
        free->setFilterFunction(filter);

        auto *used = new AggregateSensor(all, QStringLiteral("used"), QStringLiteral("Used Space"));
        used->setUnit(UnitByte);
        used->setVariantType(QVariant::ULongLong);
        used->setMatchSensors(QRegularExpression(QStringLiteral("^.*$")), QStringLiteral("used"));
        used->setFilterFunction(filter);

        auto *read = new AggregateSensor(all, QStringLiteral("read"), QStringLiteral("Read Rate"), QVariant(0.0));
        read->setUnit(UnitByteRate);
        read->setVariantType(QVariant::Double);
        read->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("read"));
        read->setFilterFunction(filter);

        auto *write = new AggregateSensor(all, QStringLiteral("write"), QStringLiteral("Write Rate"), QVariant(0.0));
        write->setUnit(UnitByteRate);
        write->setVariantType(QVariant::Double);
        write->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("write"));
        write->setFilterFunction(filter);

        auto *freePct = new PercentageSensor(all, QStringLiteral("freePercent"), QStringLiteral("Percentage Free"));
        freePct->setBaseSensor(free);
        auto *usedPct = new PercentageSensor(all, QStringLiteral("usedPercent"), QStringLiteral("Percentage Used"));
        usedPct->setBaseSensor(used);
    }

    void updateDisk()
    {
        // Update disk usage via statvfs
        for (auto *vol : m_diskVolumes) {
            if (vol->isSubscribed()) {
                vol->updateDiskUsage();
            }
        }

        // Ensure aggregate used/free max matches total
        auto *all = m_diskContainer->object(QStringLiteral("all"));
        if (all) {
            auto *totalSensor = all->sensor(QStringLiteral("total"));
            auto *usedSensor = all->sensor(QStringLiteral("used"));
            auto *freeSensor = all->sensor(QStringLiteral("free"));
            if (totalSensor && totalSensor->value().isValid()) {
                qulonglong t = totalSensor->value().toULongLong();
                if (usedSensor) usedSensor->setMax(t);
                if (freeSensor) freeSensor->setMax(t);
            }
        }

        // Update I/O rates from /proc/diskstats
        QFile ds(QStringLiteral("/proc/diskstats"));
        if (!ds.open(QIODevice::ReadOnly)) {
            return;
        }
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QMap<QString, QPair<quint64, quint64>> current;
        const auto lines = ds.readAll().split('\n');
        for (const auto &raw : lines) {
            if (raw.isEmpty()) continue;
            const auto fields = raw.simplified().split(' ');
            if (fields.size() < 14) continue;
            const QString name = QString::fromUtf8(fields[2]);
            current[name] = {fields[5].toULongLong() * 512, fields[9].toULongLong() * 512};
        }

        if (m_lastDiskTs != 0) {
            double elapsed = (now - m_lastDiskTs) / 1000.0;
            if (elapsed > 0) {
                for (auto *vol : m_diskVolumes) {
                    if (!vol->isSubscribed()) continue;
                    auto it = current.constFind(vol->deviceId());
                    if (it == current.constEnd()) continue;
                    auto prev = m_lastDiskIo.value(vol->deviceId());
                    double readRate = (it->first - prev.first) / elapsed;
                    double writeRate = (it->second - prev.second) / elapsed;
                    vol->setIORates(readRate, writeRate);
                }
            }
        }
        m_lastDiskTs = now;
        m_lastDiskIo = current;
    }

    void initNetwork()
    {
        m_netContainer = new SensorContainer(QStringLiteral("network"), QStringLiteral("Network"), this);
        m_netInterfaces.clear();

        QDir sysNet(QStringLiteral("/sys/class/net"));
        const auto ifaces = sysNet.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &ifname : ifaces) {
            if (ifname == QStringLiteral("lo")) continue;
            if (!isUsableInterface(ifname)) continue;
            m_netInterfaces.append(new NetInterface(ifname, m_netContainer));
        }
        addNetworkAggregates();
    }

    // Keep only interfaces that are actually meaningful: have an IP address,
    // or are the main data/WiFi interfaces. Skip the many virtual/tunnel ifaces.
    static bool isUsableInterface(const QString &ifname)
    {
        const auto iface = QNetworkInterface::interfaceFromName(ifname);
        const auto addrs = iface.addressEntries();
        for (const auto &entry : addrs) {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol || (ip.protocol() == QAbstractSocket::IPv6Protocol && !ip.isLinkLocal())) {
                return true;
            }
        }
        // IP-less but real data interfaces
        if (ifname.startsWith(QStringLiteral("wlan")) || ifname.startsWith(QStringLiteral("wlp"))
            || ifname.startsWith(QStringLiteral("enp")) || ifname.startsWith(QStringLiteral("eth"))
            || ifname.startsWith(QStringLiteral("rmnet_ipa")) || ifname.startsWith(QStringLiteral("rmnet_data"))) {
            return true;
        }
        return false;
    }

    void addNetworkAggregates()
    {
        auto all = new SensorObject(QStringLiteral("all"), QStringLiteral("All Network Devices"), m_netContainer);
        auto filter = [](const SensorProperty *s) {
            return s->parentObject()->id() != QStringLiteral("all");
        };

        auto *dl = new AggregateSensor(all, QStringLiteral("download"), QStringLiteral("Download Rate"), QVariant(0.0));
        dl->setUnit(UnitByteRate);
        dl->setVariantType(QVariant::Double);
        dl->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("download"));
        dl->setFilterFunction(filter);

        auto *ul = new AggregateSensor(all, QStringLiteral("upload"), QStringLiteral("Upload Rate"), QVariant(0.0));
        ul->setUnit(UnitByteRate);
        ul->setVariantType(QVariant::Double);
        ul->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("upload"));
        ul->setFilterFunction(filter);

        auto *dlBits = new AggregateSensor(all, QStringLiteral("downloadBits"), QStringLiteral("Download Rate"), QVariant(0.0));
        dlBits->setUnit(UnitBitRate);
        dlBits->setVariantType(QVariant::Double);
        dlBits->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("downloadBits"));
        dlBits->setFilterFunction(filter);

        auto *ulBits = new AggregateSensor(all, QStringLiteral("uploadBits"), QStringLiteral("Upload Rate"), QVariant(0.0));
        ulBits->setUnit(UnitBitRate);
        ulBits->setVariantType(QVariant::Double);
        ulBits->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("uploadBits"));
        ulBits->setFilterFunction(filter);

        auto *totalDl = new AggregateSensor(all, QStringLiteral("totalDownload"), QStringLiteral("Total Downloaded"), QVariant(0.0));
        totalDl->setUnit(UnitByte);
        totalDl->setVariantType(QVariant::Double);
        totalDl->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("totalDownload"));
        totalDl->setFilterFunction(filter);

        auto *totalUl = new AggregateSensor(all, QStringLiteral("totalUpload"), QStringLiteral("Total Uploaded"), QVariant(0.0));
        totalUl->setUnit(UnitByte);
        totalUl->setVariantType(QVariant::Double);
        totalUl->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("totalUpload"));
        totalUl->setFilterFunction(filter);
    }

    void updateNetwork()
    {
        QFile nd(QStringLiteral("/proc/net/dev"));
        if (!nd.open(QIODevice::ReadOnly)) {
            return;
        }
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QMap<QString, QPair<quint64, quint64>> current;
        const auto lines = nd.readAll().split('\n');
        for (const auto &raw : lines) {
            if (!raw.contains(':')) continue;
            int colon = raw.indexOf(':');
            QString ifname = raw.left(colon).trimmed();
            const auto fields = QString::fromUtf8(raw.mid(colon + 1)).simplified().split(' ');
            if (fields.size() < 10) continue;
            current[ifname] = {fields[0].toULongLong(), fields[8].toULongLong()};
        }

        // Update rates and totals
        for (auto *netif : m_netInterfaces) {
            if (!netif->isSubscribed()) continue;
            auto it = current.constFind(netif->ifname());
            if (it == current.constEnd()) continue;
            netif->setTotals(it->first, it->second);
        }

        if (m_lastNetTs != 0) {
            double elapsed = (now - m_lastNetTs) / 1000.0;
            if (elapsed > 0) {
                for (auto *netif : m_netInterfaces) {
                    if (!netif->isSubscribed()) continue;
                    auto it = current.constFind(netif->ifname());
                    if (it == current.constEnd()) continue;
                    auto prev = m_lastNetIo.value(netif->ifname());
                    double rx = (it->first - prev.first) / elapsed;
                    double tx = (it->second - prev.second) / elapsed;
                    netif->setTraffic(qMax(rx, 0.0), qMax(tx, 0.0));
                }
            }
        }
        m_lastNetTs = now;
        m_lastNetIo = current;
    }

    SensorContainer *m_diskContainer = nullptr;
    QList<DiskVolume *> m_diskVolumes;
    qint64 m_lastDiskTs = 0;
    QMap<QString, QPair<quint64, quint64>> m_lastDiskIo;

    SensorContainer *m_netContainer = nullptr;
    QList<NetInterface *> m_netInterfaces;
    qint64 m_lastNetTs = 0;
    QMap<QString, QPair<quint64, quint64>> m_lastNetIo;

    void initCpuTemp()
    {
        m_cpuTempPath.clear();
        for (int i = 0; i < 100; ++i) {
            QString zonePath = QStringLiteral("/sys/class/thermal/thermal_zone%1").arg(i);
            QFile typeFile(zonePath + QStringLiteral("/type"));
            if (!typeFile.open(QIODevice::ReadOnly)) continue;
            QString type = QString::fromUtf8(typeFile.readAll()).trimmed();
            if (type.startsWith(QStringLiteral("cpu-"))) {
                m_cpuTempPath = zonePath + QStringLiteral("/temp");
                break;
            }
        }
        if (m_cpuTempPath.isEmpty()) {
            m_cpuTempPath = QStringLiteral("/sys/class/thermal/thermal_zone5/temp");
        }

        m_cpuContainer = new SensorContainer(QStringLiteral("thermal"), QStringLiteral("Thermal"), this);
        m_cpuObj = new SensorObject(QStringLiteral("cpu"), QStringLiteral("CPU Temperature"), m_cpuContainer);
        m_cpuContainer->addObject(m_cpuObj);
        m_cpuTemp = new SysFsSensor(QStringLiteral("temperature"), m_cpuTempPath, m_cpuObj);
        m_cpuTemp->setName(QStringLiteral("Temperature"));
        m_cpuTemp->setUnit(UnitCelsius);
        m_cpuTemp->setVariantType(QVariant::Double);
        m_cpuTemp->setConvertFunction([](const QByteArray &in) {
            return QVariant(QString::fromLatin1(in).trimmed().toDouble() / 1000.0);
        });
    }

    void updateCpuTemp()
    {
        m_cpuTemp->update();
    }

    SensorContainer *m_cpuContainer = nullptr;
    SensorObject *m_cpuObj = nullptr;
    SysFsSensor *m_cpuTemp = nullptr;
    QString m_cpuTempPath;
};

K_PLUGIN_CLASS_WITH_JSON(ContainerPlugin, "metadata.json")
#include "containerplugin.moc"