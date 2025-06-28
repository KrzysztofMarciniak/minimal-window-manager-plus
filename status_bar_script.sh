#!/bin/sh
# Outputs a single status line (≤256 chars) for mwm+ bar.
# Called in a forked child process, toggle status bar to kill (Modkey + b)
while :; do
  DATE=$(date '+%Y-%m-%d %H:%M:%S')

  # Battery
  BAT_PATH="/sys/class/power_supply/BAT0"
  if [ -f "$BAT_PATH/capacity" ]; then
    BAT_CAP=$(cat "$BAT_PATH/capacity")
    case $(cat "$BAT_PATH/status" 2>/dev/null) in
      Discharging) BAT_S="(dis)" ;;
      Charging)    BAT_S="(chr)" ;;
      Full)        BAT_S="(ful)" ;;
      *)           BAT_S="" ;;
    esac
  else
    BAT_CAP="N/A"
    BAT_S=""
  fi

  # Memory
  MEM_TOTAL=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
  MEM_FREE=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
  MEM_USED=$((MEM_TOTAL - MEM_FREE))
  MEM_PCT=$((MEM_USED * 100 / MEM_TOTAL))

  # CPU
  set -- $(grep '^cpu ' /proc/stat)
  U1=$2; N1=$3; S1=$4; I1=$5; W1=$6; IRQ1=$7; SIRQ1=$8; ST1=$9
  sleep 1
  set -- $(grep '^cpu ' /proc/stat)
  U2=$2; N2=$3; S2=$4; I2=$5; W2=$6; IRQ2=$7; SIRQ2=$8; ST2=$9
  IDLE1=$((I1 + W1)); IDLE2=$((I2 + W2))
  TOTAL1=$((U1 + N1 + S1 + I1 + W1 + IRQ1 + SIRQ1 + ST1))
  TOTAL2=$((U2 + N2 + S2 + I2 + W2 + IRQ2 + SIRQ2 + ST2))
  CPU_PCT=$((100 * (TOTAL2 - TOTAL1 - (IDLE2 - IDLE1)) / (TOTAL2 - TOTAL1)))

  # Temp
  TEMP="N/A"
  for tfile in /sys/class/hwmon/hwmon*/temp1_input; do
    [ -f "$tfile" ] || continue
    T=$(cat "$tfile")
    TEMP=$((T / 1000))
    break
  done

  # Network
  NET_IF=""; NET_IP=""
  for iface in /sys/class/net/*; do
    IF=$(basename "$iface")
    [ "$(cat "$iface/operstate" 2>/dev/null)" = "up" ] || continue
    IP=$(ip -4 -o addr show "$IF" | awk '{print $4}' | cut -d/ -f1 | head -n1)
    if [ -n "$IP" ]; then
      NET_IF=$IF
      NET_IP=$IP
      break
    fi
  done

  printf "%s | b:%s%% %s | m:%d%% | cpu:%d%% temp:%s | n:%s (%s)\n" \
    "$DATE" "$BAT_CAP" "$BAT_S" "$MEM_PCT" "$CPU_PCT" "$TEMP" "$NET_IF" "$NET_IP"
done
