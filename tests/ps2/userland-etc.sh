#!/bin/sh
# usage: userland-etc.sh <tree> <tools-bin>   Populate /etc and friends for the PS2 root.
set -eu
tree=$1; tools=$2
etc=$tree/etc

cat >"$etc/master.passwd" <<'X'
root::0:0::0:0:Charlie &:/root:/bin/sh
daemon:*:1:1::0:0:The devil himself:/:/sbin/nologin
nobody:*:32767:39:Unprivileged user:0:0:Nobody:/nonexistent:/sbin/nologin
ps2::1000:100::0:0:PS2 user:/home/ps2:/bin/sh
_dhcpcd:*:211:211::0:0:dhcpcd:/var/chroot/dhcpcd:/sbin/nologin
X
cat >"$etc/group" <<'X'
wheel:*:0:root
daemon:*:1:
kmem:*:2:
sys:*:3:
tty:*:4:
operator:*:5:root
bin:*:7:
users:*:100:
_dhcpcd:*:211:
nogroup:*:39:
X
"$tools/nbpwd_mkdb" -d "$tree" -p "$etc/master.passwd" 2>&1 || {
    echo "pwd_mkdb failed; falling back to text passwd" >&2
    awk -F: '{printf "%s:*:%s:%s:%s:%s:%s\n",$1,$3,$4,$8,$9,$10}' "$etc/master.passwd" >"$etc/passwd"
}
chmod 600 "$etc/master.passwd"
mkdir -p "$tree/home/ps2"
chmod 755 "$tree/home/ps2"
chmod 700 "$tree/root"
chmod 1777 "$tree/tmp" "$tree/var/tmp"

cat >"$etc/fstab" <<'X'
/dev/wd0a	/	ffs	rw	1 1
/dev/wd1b	none	swap	sw	0 0
ptyfs	/dev/pts	ptyfs	rw	0 0
X
cat >"$etc/ttys" <<'X'
# name  getty                         type    status  comment
console "/usr/libexec/getty std.9600" vt100   on secure
ttyE0   "/usr/libexec/getty Pc"       vt100   on secure
ttyE1   "/usr/libexec/getty Pc"       vt100   on secure
ttyE2   "/usr/libexec/getty Pc"       vt100   on secure
ttyE3   "/usr/libexec/getty Pc"       vt100   off secure
X
cat >"$etc/gettytab" <<'X'
default:\
	:np:im=\r\n%s/%m (%h) (%t)\r\n\r\n:sp#9600:
std.9600|9600-baud:\
	:sp#9600:
Pc|Pc console:\
	:np:im=\r\n%s/%m (%h) (%t)\r\n\r\n:sp#9600:
X
cat >"$etc/shells" <<'X'
/bin/sh
/bin/ksh
/bin/csh
X
printf 'ps2\n' >"$etc/myname"
# PCSX2's intercepted DHCP gives no DNS server; dhcpcd appends this to resolv.conf.
printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' >"$etc/resolv.conf.tail"
cat >"$etc/hosts" <<'X'
127.0.0.1	localhost
X
printf '\nWelcome to NetBSD/playstation2 (no SBIOS)\n\n' >"$etc/motd"
cat >"$etc/profile" <<'X'
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R7/bin:/usr/pkg/bin
export HOME=${HOME:-/root}
export TERM=${TERM:-vt100}
export ENV=/etc/shrc
X
cat >"$etc/shrc" <<'X'
if [ "$(id -u)" = 0 ]; then PS1='ps2# '; else PS1='ps2$ '; fi
alias ll='ls -l'
X
# The real /etc/rc, rc.subr, defaults/rc.conf and rc.d/* from the source tree.
srcetc=etc
mkdir -p "$etc/defaults" "$etc/rc.d"
cp "$srcetc/rc" "$srcetc/rc.subr" "$srcetc/rc.shutdown" "$srcetc/rc.conf" "$etc/"
cp "$srcetc/defaults/rc.conf" "$etc/defaults/rc.conf"
cp "$srcetc"/rc.d/* "$etc/rc.d/" 2>/dev/null || true
rm -f "$etc/rc.d/Makefile" "$etc/rc.d"/*.in
chmod 755 "$etc/rc" "$etc/rc.shutdown"
chmod 555 "$etc/rc.d"/*
chmod 644 "$etc/rc.subr" "$etc/rc.conf" "$etc/defaults/rc.conf"
# PS2 overrides.  /etc/rc.conf is read after defaults/rc.conf.
cat >"$etc/rc.conf" <<'X'
# NetBSD/playstation2 rc.conf (overrides /etc/defaults/rc.conf)
if [ -r /etc/defaults/rc.conf ]; then
	. /etc/defaults/rc.conf
fi
rc_configured=YES
hostname=ps2
swapoff=YES
savecore=NO
ccd=NO
raidframe=NO
cgd=NO
quota=NO
sysdb=NO
modules=NO
ppp=NO
makemandb=NO
fccache=NO
iscsid_volumes=NO
virecover=NO
bthcid=NO
sdpd=NO
random_seed=NO
postfix=NO
update_motd=NO
do_rcshutdown=YES
auto_ifconfig=NO
net_interfaces="smap0"
ifconfig_smap0=dhcp
dhcpcd=YES
dhcpcd_flags="-qMb"
syslogd=YES
syslogd_flags=""
cron=YES
inetd=YES
inetd_flags=""
X
for f in syslog.conf inetd.conf services protocols nsswitch.conf mailer.conf; do
	cp "$srcetc/$f" "$etc/$f"
done
# Enable the IPv4 internal services (no IPv6 in this userland).
sed -E 's/^#((echo|discard|daytime|time)[[:space:]].*(tcp|udp)[[:space:]].*internal)/\1/' \
	"$srcetc/inetd.conf" >"$etc/inetd.conf"
cat >"$etc/crontab" <<'X'
SHELL=/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
HOME=/var/log
#min	hour	mday	month	wday	user	command
*	*	*	*	*	root	/bin/date >>/var/log/cron-ps2.log
X
chmod 644 "$etc/crontab"
for f in messages authlog cron xferlog lpd-errs maillog; do
	: >"$tree/var/log/$f"
done
chmod 644 "$tree/var/log/messages" "$tree/var/log/cron"
chmod 600 "$tree/var/log/authlog"
mkdir -p "$tree/var/cron/tabs" "$tree/var/db" "$tree/usr/libexec"
chmod 755 "$tree/var/cron"; chmod 700 "$tree/var/cron/tabs"
printf '#!/bin/sh\nexit 0\n' >"$tree/usr/bin/true"
printf '#!/bin/sh\nexit 1\n' >"$tree/usr/bin/false"
chmod 755 "$tree/usr/bin/true" "$tree/usr/bin/false"

# X11 (Xorg + wsfb/ws): configuration for the GS wsdisplay and USB wskbd/wsmouse.
mkdir -p "$etc/X11" "$tree/root" "$tree/var/db/xkb"
cat >"$etc/X11/xorg.conf" <<'X'
Section "ServerFlags"
	Option "AutoAddDevices" "off"
	Option "AutoEnableDevices" "off"
	Option "DontZap" "on"
EndSection
Section "Files"
	ModulePath "/usr/X11R7/lib/modules"
	FontPath "/usr/X11R7/lib/X11/fonts/misc/"
EndSection
Section "InputDevice"
	Identifier "Keyboard0"
	Driver "kbd"
EndSection
Section "InputDevice"
	Identifier "Mouse0"
	Driver "ws"
	Option "Device" "/dev/wsmouse"
EndSection
Section "Device"
	Identifier "GS0"
	Driver "wsfb"
	Option "device" "/dev/ttyE0"
	Option "ShadowFB" "off"
EndSection
Section "Screen"
	Identifier "Screen0"
	Device "GS0"
EndSection
Section "ServerLayout"
	Identifier "Layout0"
	Screen "Screen0"
	InputDevice "Keyboard0" "CoreKeyboard"
	InputDevice "Mouse0" "CorePointer"
EndSection
X
cat >"$tree/root/.xinitrc" <<'X'
export FONTCONFIG_FILE=/etc/fonts/fonts.conf
xsetroot -solid steelblue
xclock -geometry 100x100-0+0 &
xterm -geometry 80x24+10+10 -bg black -fg green &
[ -x /usr/pkg/bin/dillo ] && dillo -g 560x380+40+60 &
exec twm
X
mkdir -p "$etc/fonts" "$tree/var/cache/fontconfig"
cat >"$etc/fonts/fonts.conf" <<'X'
<?xml version="1.0"?>
<fontconfig>
	<dir>/usr/X11R7/lib/X11/fonts/xft</dir>
	<cachedir>/var/cache/fontconfig</cachedir>
</fontconfig>
X
