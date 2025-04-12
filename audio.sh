#!/bin/bash
notifications_daemon="/usr/local/bin/herbe"

control_volume() {
    VOL_STATUS=$(amixer get Master | awk -F'[][]' '/%/ { print $2; exit }')
    if [ "$1" == "+" ]; then
        amixer set Master 5%+ > /dev/null
    elif [ "$1" == "-" ]; then
        amixer set Master 5%- > /dev/null
    else
        exit 1
    fi
    NEW_VOL=$(amixer get Master | awk -F'[][]' '/%/ { print $2; exit }')
    if [ "$VOL_STATUS" != "$NEW_VOL" ]; then
        pkill -x $(basename "$notifications_daemon")
        MSG="vol $NEW_VOL"
        [[ "$NEW_VOL" == "100%" ]] && MSG="vol max"
        [[ "$NEW_VOL" == "0%" ]] && MSG="vol zero"
        $notifications_daemon "$MSG" &
        sleep 1
        pkill -x $(basename "$notifications_daemon")
    fi
}

toggle_audio() {
    STATUS=$(amixer get Master | grep '\[off\]' -c)
    if [ "$STATUS" -eq 0 ]; then
        amixer set Master mute
        MESSAGE="aud off"
    else
        amixer set Master unmute
        MESSAGE="aud on"
    fi
    $notifications_daemon "$MESSAGE" &
    sleep 1
    pkill -x $(basename "$notifications_daemon")
}

toggle_mic() {
    STATUS=$(amixer get Capture | grep '\[off\]' -c)
    if [ "$STATUS" -eq 0 ]; then
        amixer set Capture nocap
        MESSAGE="mic off"
    else
        amixer set Capture cap
        MESSAGE="mic on"
    fi
    $notifications_daemon "$MESSAGE" &
    sleep 1
    pkill -x $(basename "$notifications_daemon")
}

case "$1" in
    "+")
        control_volume "+"
        ;;
    "-")
        control_volume "-"
        ;;
    "aud")
        toggle_audio
        ;;
    "mic")
        toggle_mic
        ;;
    *)
        echo "Usage: $0 {+ | - | aud | mic}"
        exit 1
        ;;
esac
