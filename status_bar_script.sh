#!/bin/sh
# Outputs a single status line (≤256 chars) for mwm+ bar.
# Called in a forked child process, toggle status bar to kill (Modkey + b)
while :; do
  DATE=$(date '+%Y-%m-%d %H:%M:%S')

  # Battery
  BAT_PATH="/sys/class/power_supply/BAT0"
  BAT_CAP=$(cat "$BAT_PATH/capacity" 2>/dev/null || echo "N/A")
  case "$(cat "$BAT_PATH/status" 2>/dev/null)" in
    Discharging) BAT_S="(dis)" ;;
    Charging)    BAT_S="(chr)" ;;
    Full)        BAT_S="(ful)" ;;
    *)           BAT_S="" ;;
  esac

  # Memory
  read _ MEM_TOTAL _ < <(grep MemTotal /proc/meminfo)
  read _ MEM_FREE _  < <(grep MemAvailable /proc/meminfo)
  MEM_USED=$((MEM_TOTAL - MEM_FREE))
  MEM_PCT=$((MEM_USED * 100 / MEM_TOTAL))

  # CPU
  read _ u1 n1 s1 i1 w1 ir1 si1 st1 _ < /proc/stat
  sleep 1
  read _ u2 n2 s2 i2 w2 ir2 si2 st2 _ < /proc/stat
  IDLE1=$((i1 + w1)); IDLE2=$((i2 + w2))
  TOTAL1=$((u1 + n1 + s1 + i1 + w1 + ir1 + si1 + st1))
  TOTAL2=$((u2 + n2 + s2 + i2 + w2 + ir2 + si2 + st2))
  CPU_PCT=$((100 * (TOTAL2 - TOTAL1 - (IDLE2 - IDLE1)) / (TOTAL2 - TOTAL1) ))

  # Temp
  for tfile in /sys/class/hwmon/hwmon*/temp1_input; do
    [ -f "$tfile" ] && TEMP=$(( $(cat "$tfile") / 1000 )) && break
  done
  TEMP=${TEMP:-"N/A"}

  # Network
  NET_IF=""; NET_IP=""
  for iface in /sys/class/net/*; do
    iface=$(basename "$iface")
    [ "$(cat /sys/class/net/$iface/operstate 2>/dev/null)" = "up" ] || continue
    ip=$(ip -4 -o addr show "$iface" | awk '{print $4}' | cut -d/ -f1 | head -1)
    if [ -n "$ip" ]; then
      NET_IF=$iface
      NET_IP=$ip
      break
    fi
  done

  printf "%s | b:%s%% %s | m:%d%% | cpu:%d%% temp:%s | n:%s (%s)\n" \
    "$DATE" "$BAT_CAP" "$BAT_S" "$MEM_PCT" "$CPU_PCT" "$TEMP" "$NET_IF" "$NET_IP"
done