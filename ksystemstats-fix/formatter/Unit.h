#pragma once

// Simplified copy of the KSysGuard Unit enum, preserving the exact numeric
// values so that values passed to SensorProperty::setUnit() match libKSysGuardSystemStats.
//
// SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
// SPDX-License-Identifier: LGPL-2.0-or-later

namespace KSysGuard
{
enum MetricPrefix {
    MetricPrefixAutoAdjust = -1,
    MetricPrefixUnity = 0,
    MetricPrefixKilo,
    MetricPrefixMega,
    MetricPrefixGiga,
    MetricPrefixTera,
    MetricPrefixPeta,
    MetricPrefixLast = MetricPrefixPeta
};

enum Unit {
    UnitInvalid = -1,
    UnitNone = 0,

    UnitByte = 100,
    UnitKiloByte = MetricPrefixKilo + UnitByte,
    UnitMegaByte = MetricPrefixMega + UnitByte,
    UnitGigaByte = MetricPrefixGiga + UnitByte,
    UnitTeraByte = MetricPrefixTera + UnitByte,
    UnitPetaByte = MetricPrefixPeta + UnitByte,

    UnitByteRate = 200,
    UnitKiloByteRate = MetricPrefixKilo + UnitByteRate,
    UnitMegaByteRate = MetricPrefixMega + UnitByteRate,
    UnitGigaByteRate = MetricPrefixGiga + UnitByteRate,
    UnitTeraByteRate = MetricPrefixTera + UnitByteRate,
    UnitPetaByteRate = MetricPrefixPeta + UnitByteRate,

    UnitHertz = 300,
    UnitKiloHertz = MetricPrefixKilo + UnitHertz,
    UnitMegaHertz = MetricPrefixMega + UnitHertz,
    UnitGigaHertz = MetricPrefixGiga + UnitHertz,
    UnitTeraHertz = MetricPrefixTera + UnitHertz,
    UnitPetaHertz = MetricPrefixPeta + UnitHertz,

    UnitBootTimestamp = 400,
    UnitSecond,
    UnitTime,
    UnitTicks,
    UnitDuration,

    UnitBitRate = 500,
    UnitKiloBitRate = MetricPrefixKilo + UnitBitRate,
    UnitMegaBitRate = MetricPrefixMega + UnitBitRate,
    UnitGigaBitRate = MetricPrefixGiga + UnitBitRate,
    UnitTeraBitRate = MetricPrefixTera + UnitBitRate,
    UnitPetaBitRate = MetricPrefixPeta + UnitBitRate,

    UnitVolt = 600,
    UnitKiloVolt = MetricPrefixKilo + UnitVolt,
    UnitMegaVolt = MetricPrefixMega + UnitVolt,
    UnitGigaVolt = MetricPrefixGiga + UnitVolt,
    UnitTeraVolt = MetricPrefixTera + UnitVolt,
    UnitPetaVolt = MetricPrefixPeta + UnitVolt,

    UnitWatt = 700,
    UnitKiloWatt = MetricPrefixKilo + UnitWatt,
    UnitMegaWatt = MetricPrefixMega + UnitWatt,
    UnitGigaWatt = MetricPrefixGiga + UnitWatt,
    UnitTeraWatt = MetricPrefixTera + UnitWatt,
    UnitPetaWatt = MetricPrefixPeta + UnitWatt,

    UnitWattHour = 800,
    UnitKiloWattHour = MetricPrefixKilo + UnitWattHour,
    UnitMegaWattHour = MetricPrefixMega + UnitWattHour,
    UnitGigaWattHour = MetricPrefixGiga + UnitWattHour,
    UnitTeraWattHour = MetricPrefixTera + UnitWattHour,
    UnitPetaWattHour = MetricPrefixPeta + UnitWattHour,

    UnitAmpere = 900,
    UnitKiloAmpere = MetricPrefixKilo + UnitAmpere,
    UnitMegaAmpere = MetricPrefixMega + UnitAmpere,
    UnitGigaAmpere = MetricPrefixGiga + UnitAmpere,
    UnitTeraAmpere = MetricPrefixTera + UnitAmpere,
    UnitPetaAmpere = MetricPrefixPeta + UnitAmpere,

    UnitCelsius = 1000,
    UnitDecibelMilliWatts,
    UnitPercent,
    UnitRate,
    UnitRpm,
};
} // namespace KSysGuard
