#!/bin/sh
# Sound from https://notificationsounds.com/

export DISPLAY=:0

from=$1
to=$2
txt=$3
[ "$from" = "$to" ] && to="private"

btn="Read"

mpv circo_notify.ogg >/dev/null 2>&1 &
xmessage -geom 250x100 -button "$btn" -default "$btn" -timeout 3 -center "$from in $to

$txt"
