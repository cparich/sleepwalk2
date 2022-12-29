# sleepwalk2
A sleepwalk implementation for PinePhone and PinePhone Pro

Heavy inspiration taken from

* https://github.com/milky-sway/pinephone-scripts

Requires C++20, Qt5.15, libfmt-dev but you can probably get it working with less

This program uses systemd and gnome dbus services to poll for inhibits and perform sleep commands.

Also takes some control of the front LED.

Should properly handle both the OG PinePhone and PinePhone Pro, PRs welcome if you find it's broken.

Working:

* Notification Detection
* Call Inhibit
* Sleep
* Sleepwalk

ToDo:

* Detect Wifi Hotspot
* Detect Audio Playing
