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
X
cat >"$etc/ttys" <<'X'
# name  getty                         type    status  comment
console "/usr/libexec/getty std.9600" vt100   on secure
X
cat >"$etc/gettytab" <<'X'
default:\
	:np:im=\r\n%s/%m (%h) (%t)\r\n\r\n:sp#9600:
std.9600|9600-baud:\
	:sp#9600:
X
cat >"$etc/shells" <<'X'
/bin/sh
/bin/ksh
/bin/csh
X
printf 'ps2\n' >"$etc/myname"
cat >"$etc/hosts" <<'X'
127.0.0.1	localhost
X
printf '\nWelcome to NetBSD/playstation2 (no SBIOS)\n\n' >"$etc/motd"
cat >"$etc/profile" <<'X'
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
export HOME=${HOME:-/root}
export TERM=${TERM:-vt100}
export ENV=/etc/shrc
X
cat >"$etc/shrc" <<'X'
if [ "$(id -u)" = 0 ]; then PS1='ps2# '; else PS1='ps2$ '; fi
alias ll='ls -l'
X
cat >"$etc/rc.conf" <<'X'
hostname=ps2
swapctl=YES
syslogd=NO
X
cat >"$etc/rc" <<'X'
#!/bin/sh
# Minimal /etc/rc: remount root, swap, hostname, sysctl, local services.
PATH=/sbin:/bin:/usr/sbin:/usr/bin
export PATH
exec </dev/console >/dev/console 2>&1
. /etc/rc.conf
echo "Starting PS2 rc..."
mount -u -o rw / 2>/dev/null || mount -u -o rw /dev/wd0a / 2>/dev/null
mount -a -t ffs 2>/dev/null
[ "$swapctl" = YES ] && swapctl -A 2>/dev/null
hostname "$hostname"
[ -f /etc/sysctl.conf ] && while read k; do sysctl -w "$k" >/dev/null; done </etc/sysctl.conf
dmesg >/var/run/dmesg.boot 2>/dev/null
: >/var/run/utmpx 2>/dev/null
for s in /etc/rc.d/*; do
	[ -x "$s" ] && "$s" start
done
echo "rc done"
exit 0
X
chmod 755 "$etc/rc"
mkdir -p "$etc/rc.d"
printf '#!/bin/sh\nexit 0\n' >"$tree/usr/bin/true"
printf '#!/bin/sh\nexit 1\n' >"$tree/usr/bin/false"
chmod 755 "$tree/usr/bin/true" "$tree/usr/bin/false"
