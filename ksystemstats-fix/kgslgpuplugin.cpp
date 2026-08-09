/*
 * ksystemstats plugin that exposes the Qualcomm Adreno GPU (KGSL) as sensors.
 *
 * Reads live data from /sys/class/kgsl/kgsl-3d0/:
 *   gpu_busy_percentage -> usage (%)
 *   gpuclk              -> current frequency (Hz)
 *   temp                -> temperature (milli-celsius)
 *
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KPluginFactory>

#include <QFile>
#include <QRegularExpression>

#include <systemstats/AggregateSensor.h>
#include <systemstats/SensorContainer.h>
#include <systemstats/SensorObject.h>
#include <systemstats/SensorPlugin.h>
#include <systemstats/SensorProperty.h>
#include <systemstats/SysFsSensor.h>

using namespace KSysGuard;

namespace
{
const QString kgslPath = QStringLiteral("/sys/class/kgsl/kgsl-3d0/");

QString readString(const QString &fileName)
{
    QFile file(kgslPath + fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(file.readAll()).trimmed();
}

QVariant percentToValue(const QByteArray &input)
{
    QString s = QString::fromLatin1(input).trimmed();
    s.remove(QLatin1Char('%'));
    return QVariant(s.toDouble());
}

QVariant hzToMHz(const QByteArray &input)
{
    return QVariant(QString::fromLatin1(input).trimmed().toDouble() / 1e6);
}

QVariant milliCelsiusToCelsius(const QByteArray &input)
{
    return QVariant(QString::fromLatin1(input).trimmed().toDouble() / 1000.0);
}
} // namespace

class KgslGpuPlugin : public SensorPlugin
{
    Q_OBJECT
public:
    KgslGpuPlugin(QObject *parent, const QVariantList &args)
        : SensorPlugin(parent, args)
    {
        m_container = new SensorContainer(QStringLiteral("gpu"), QStringLiteral("GPU"), this);

        m_gpu0 = new SensorObject(QStringLiteral("gpu0"), QStringLiteral("GPU"), m_container);
        m_container->addObject(m_gpu0);

        auto *nameProperty = new SensorProperty(QStringLiteral("name"), m_gpu0);
        nameProperty->setValue(readString(QStringLiteral("gpu_model")));

        m_usage = new SysFsSensor(QStringLiteral("usage"), kgslPath + QStringLiteral("gpu_busy_percentage"), m_gpu0);
        m_usage->setName(QStringLiteral("GPU Usage"));
        m_usage->setShortName(QStringLiteral("Usage"));
        m_usage->setUnit(UnitPercent);
        m_usage->setMax(100);
        m_usage->setVariantType(QVariant::Double);
        m_usage->setConvertFunction(percentToValue);

        m_frequency = new SysFsSensor(QStringLiteral("coreFrequency"), kgslPath + QStringLiteral("gpuclk"), m_gpu0);
        m_frequency->setName(QStringLiteral("Frequency"));
        m_frequency->setShortName(QStringLiteral("Frequency"));
        m_frequency->setUnit(UnitMegaHertz);
        m_frequency->setVariantType(QVariant::Double);
        m_frequency->setConvertFunction(hzToMHz);

        m_temperature = new SysFsSensor(QStringLiteral("temperature"), kgslPath + QStringLiteral("temp"), m_gpu0);
        m_temperature->setName(QStringLiteral("Temperature"));
        m_temperature->setShortName(QStringLiteral("Temperature"));
        m_temperature->setUnit(UnitCelsius);
        m_temperature->setVariantType(QVariant::Double);
        m_temperature->setConvertFunction(milliCelsiusToCelsius);

        m_all = new SensorObject(QStringLiteral("all"), QStringLiteral("All GPUs"), m_container);
        m_container->addObject(m_all);

        auto *allUsage = new AggregateSensor(m_all, QStringLiteral("usage"), QStringLiteral("All GPUs Usage"));
        allUsage->setShortName(QStringLiteral("Usage"));
        allUsage->setMatchSensors(QRegularExpression(QStringLiteral("^(?!all).*$")), QStringLiteral("usage"));
        allUsage->setAggregateFunction([](const QVariant &first, const QVariant &second) {
            return QVariant::fromValue(first.toDouble() + second.toDouble());
        });
        allUsage->setUnit(UnitPercent);
        allUsage->setMax(100);
        allUsage->setVariantType(QVariant::Double);
    }

    void update() override
    {
        m_usage->update();
        m_frequency->update();
        m_temperature->update();
    }

    QString providerName() const override
    {
        return QStringLiteral("kgslgpu");
    }

private:
    SensorContainer *m_container = nullptr;
    SensorObject *m_gpu0 = nullptr;
    SensorObject *m_all = nullptr;
    SysFsSensor *m_usage = nullptr;
    SysFsSensor *m_frequency = nullptr;
    SysFsSensor *m_temperature = nullptr;
};

K_PLUGIN_CLASS_WITH_JSON(KgslGpuPlugin, "metadata.json")

#include "kgslgpuplugin.moc"
