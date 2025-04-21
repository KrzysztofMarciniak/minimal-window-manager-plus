#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define NOTIFICATIONS_DAEMON "/usr/local/bin/herbe"
#define BUFFER_SIZE 128
char* execute_command(const char* cmd) {
  FILE* pipe = popen(cmd, "r");
  if (!pipe) {
    return NULL;
  }
  static char buffer[BUFFER_SIZE];
  if (fgets(buffer, BUFFER_SIZE, pipe) != NULL) {
    pclose(pipe);
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    return buffer;
  }

  pclose(pipe);
  return NULL;
}
void execute_command_no_output(const char* cmd) { system(cmd); }
void kill_notification_daemon() {
  char cmd[BUFFER_SIZE];
  snprintf(cmd, BUFFER_SIZE, "pkill -x %s", basename((char*)NOTIFICATIONS_DAEMON));
  system(cmd);
}
void show_notification(const char* message) {
  char cmd[BUFFER_SIZE];
  kill_notification_daemon();
  snprintf(cmd, BUFFER_SIZE, "%s \"%s\" &", NOTIFICATIONS_DAEMON, message);
  system(cmd);
  sleep(1);
  kill_notification_daemon();
}
char* get_volume() {
  static char volume[BUFFER_SIZE];
  char* result = execute_command("amixer get Master | awk -F'[][]' '/%/ { print $2; exit }'");
  if (result) {
    strncpy(volume, result, BUFFER_SIZE);
    return volume;
  }
  return NULL;
}
void control_volume(const char* direction) {
  if (strcmp(direction, "+") == 0) {
    execute_command_no_output("amixer set Master 5%+ > /dev/null");
  } else if (strcmp(direction, "-") == 0) {
    execute_command_no_output("amixer set Master 5%- > /dev/null");
  } else {
    fprintf(stderr, "Invalid volume control direction\n");
    return;
  }
  char* new_vol = get_volume();
  if (new_vol) {
    char msg[BUFFER_SIZE];
    snprintf(msg, BUFFER_SIZE, "vol %s", new_vol);
    show_notification(msg);
  }
}
void toggle_audio() {
  int status = atoi(execute_command("amixer get Master | grep '\\[off\\]' -c"));
  if (status == 0) {
    execute_command_no_output("amixer set Master mute");
    show_notification("aud off");
  } else {
    execute_command_no_output("amixer set Master unmute");
    show_notification("aud on");
  }
}
void toggle_mic() {
  int status = atoi(execute_command("amixer get Capture | grep '\\[off\\]' -c"));
  if (status == 0) {
    execute_command_no_output("amixer set Capture nocap");
    show_notification("mic off");
  } else {
    execute_command_no_output("amixer set Capture cap");
    show_notification("mic on");
  }
}
int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s {+ | - | aud | mic}\n", argv[0]);
    return 1;
  }
  if (strcmp(argv[1], "+") == 0) {
    control_volume("+");
  } else if (strcmp(argv[1], "-") == 0) {
    control_volume("-");
  } else if (strcmp(argv[1], "aud") == 0) {
    toggle_audio();
  } else if (strcmp(argv[1], "mic") == 0) {
    toggle_mic();
  } else {
    fprintf(stderr, "Usage: %s {+ | - | aud | mic}\n", argv[0]);
    return 1;
  }
  return 0;
}
