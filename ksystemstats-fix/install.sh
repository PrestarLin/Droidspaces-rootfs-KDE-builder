#!/bin/bash
#
# Install pre-built custom ksystemstats plugins on a new system.
#   - disables the official GPU/disk/network plugins (dpkg-divert)
#   - installs the pre-built kgslgpu + containerio plugins
#   - patches the system overview page config
#   - restarts ksystemstats
#
# Usage: bash install.sh
# Prompts for sudo password when needed.
set -euo pipefail

cd "$(dirname "$0")"

# Resolve the plugin directory (adjust if your distro differs)
PLUGDIR=$(kf6-config --path plugin 2>/dev/null | tr ':' '\n' | grep ksystemstats | head -1)
if [ -z "$PLUGDIR" ]; then
    PLUGDIR=/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats
fi
PLUGDIR="${PLUGDIR%/ksystemstats}/ksystemstats"
echo "== plugin directory: $PLUGDIR"

# File names
GPU_PLUG=ksystemstats_plugin_gpu.so
DISK_PLUG=ksystemstats_plugin_disk.so
NET_PLUG=ksystemstats_plugin_network.so
CUSTOM_GPU=ksystemstats_plugin_kgslgpu.so
CUSTOM_IO=ksystemstats_plugin_containerio.so

# 1. Disable official plugins (dpkg-divert renames them, survives upgrades)
for p in "$GPU_PLUG" "$DISK_PLUG" "$NET_PLUG"; do
    if [ -e "$PLUGDIR/$p" ] && [ ! -e "$PLUGDIR/$p.distrib" ]; then
        echo "== disabling official $p"
        sudo dpkg-divert --rename --local --add "$PLUGDIR/$p"
    else
        echo "== $p already disabled or missing, skipping"
    fi
done

# 2. Install pre-built custom plugins
echo "== installing $CUSTOM_GPU"
sudo cp "$CUSTOM_GPU" "$PLUGDIR/"
echo "== installing $CUSTOM_IO"
sudo cp "container/$CUSTOM_IO" "$PLUGDIR/"
sudo chmod 644 "$PLUGDIR/$CUSTOM_GPU" "$PLUGDIR/$CUSTOM_IO"

# 3. Restart ksystemstats
echo "== restarting ksystemstats"
if systemctl --user list-unit-files plasma-ksystemstats.service >/dev/null 2>&1; then
    systemctl --user restart plasma-ksystemstats.service
else
    killall -9 ksystemstats 2>/dev/null || true
fi
sleep 2

# 4. Check if overview page exists and offer to patch it
OVERVIEW=~/.local/share/plasma-systemmonitor/overview.page
if [ -f "$OVERVIEW" ]; then
    echo ""
    echo "== overview page found at $OVERVIEW"
    echo "   The system overview page needs to be updated to use the new sensors."
    echo "   A backup will be saved as $OVERVIEW.backup"
    echo "   Apply now? [Y/n]"
    read -r REPLY
    if [[ "$REPLY" =~ ^[Yy]?$ ]]; then
        cp "$OVERVIEW" "$OVERVIEW.backup"
        echo "backup saved to $OVERVIEW.backup"
        # Patch the network section to use line sensor
        # (the system monitor may overwrite manual edits, but this is the intended config)
        python3 -c "
import re
with open('$OVERVIEW', 'r') as f:
    content = f.read()

# Replace the network face section
old = r'''\[Face-101427258480848\]\[Appearance\]
Title=网络
chartFace=org\.kde\.ksysguard\.textonly

\[Face-101427258480848\]\[SensorColors\].*?\[Face-101427258480848\]\[Sensors\]'''

new = '''[Face-101427258480848][Appearance]
Title=网络
chartFace=org.kde.ksysguard.textonly

[Face-101427258480848][Sensors]'''

content = re.sub(old, new, content, flags=re.DOTALL)

# Update the network sensors section
old2 = r'''(?<=\[Face-101427258480848\]\[Sensors\]\n).*?(?=\n\[Face-101427258480848\]\[org\.kde\.ksysguard\.textonly\]\[General\])'''

new2 = '''highPrioritySensorIds=[\"network/(?!all).*/line\"]
totalSensors=[]'''

content = re.sub(old2, new2, content, flags=re.DOTALL)

# Update the General section
old3 = r'''(?<=\[Face-101427258480848\]\[org\.kde\.ksysguard\.textonly\]\[General\]\n).*?(?=\n\[Face-)'''

new3 = '''groupByTotal=false'''

content = re.sub(old3, new3, content, flags=re.DOTALL)

# Update CPU face to use textonly with usage/freq/temp
old4 = r'''\[Face-106123380916688\]\[Appearance\]
Title=CPU
chartFace=org\.kde\.ksysguard\.piechart'''

new4 = '''[Face-106123380916688][Appearance]
Title=CPU
chartFace=org.kde.ksysguard.textonly'''

content = re.sub(old4, new4, content)

old5 = r'''(?<=\[Face-106123380916688\]\[Sensors\]\n).*?(?=\n\[Face-106123380916688\]\[org\.kde\.ksysguard\.textonly\]\[General\])'''

new5 = '''highPrioritySensorIds=[\"cpu/all/usage\",\"cpu/all/averageFrequency\",\"thermal/cpu/temperature\"]
totalSensors=[\"cpu/all/name\"]'''

content = re.sub(old5, new5, content, flags=re.DOTALL)

# Update GPU face to use textonly with usage/freq/temp
old6 = r'''\[Face-106123406501568\]\[Appearance\]
Title=GPU
chartFace=org\.kde\.ksysguard\.piechart'''

new6 = '''[Face-106123406501568][Appearance]
Title=GPU
chartFace=org.kde.ksysguard.textonly'''

content = re.sub(old6, new6, content)

old7 = r'''(?<=\[Face-106123406501568\]\[Sensors\]\n).*?(?=\n\[Face-106123406501568\]\[org\.kde\.ksysguard\.textonly\]\[General\])'''

new7 = '''highPrioritySensorIds=[\"gpu/gpu0/usage\",\"gpu/gpu0/coreFrequency\",\"gpu/gpu0/temperature\"]
totalSensors=[\"gpu/gpu0/name\"]'''

content = re.sub(old7, new7, content, flags=re.DOTALL)

with open('$OVERVIEW', 'w') as f:
    f.write(content)
print('overview page updated')
"
    else
        echo "skipped"
    fi
fi

echo ""
echo "== done."
echo "Now verify sensors:"
echo "  busctl --user call org.kde.ksystemstats1 /org/kde/ksystemstats1 org.kde.ksystemstats1 allSensors 2>&1 | grep -oE '\"(gpu|disk|network|thermal)/[^\"]*\"' | sort -u"
echo "  python3 -c \"import dbus, time; bus = dbus.SessionBus(); svc = bus.get_object('org.kde.ksystemstats1', '/org/kde/ksystemstats1'); iface = dbus.Interface(svc, 'org.kde.ksystemstats1'); ids = ['gpu/gpu0/usage','gpu/gpu0/coreFrequency','gpu/gpu0/temperature','cpu/all/usage','cpu/all/averageFrequency','thermal/cpu/temperature','network/rmnet_ipa0/line']; iface.subscribe(ids); time.sleep(3); data = dict(iface.sensorData(ids)); [print(f'{k}: {v}') for k,v in sorted(data.items())]\""