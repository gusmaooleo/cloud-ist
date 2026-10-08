#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "parser.h"
#include "datacenter.h"
#include "constants.h"

typedef struct {
  DataCenter *dc;
  char **files;
  size_t file_count;
  size_t next_file;
  pthread_mutex_t queue_mutex;
  atomic_size_t workers_finished;
  atomic_int had_error;
  size_t worker_count;
} AppContext;

typedef struct {
  off_t offset;
  size_t line;
} ConfigPosition;

/* Track lines without changing the parser's file descriptor offset. */
static size_t config_command_line(int fd, ConfigPosition *position) {
  off_t end = lseek(fd, 0, SEEK_CUR);
  size_t command_line = position->line;
  char buffer[4096];
  while (position->offset < end) {
    off_t remaining = end - position->offset;
    size_t amount = remaining < (off_t)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
    ssize_t count = pread(fd, buffer, amount, position->offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    for (ssize_t i = 0; i < count; i++) {
      if (buffer[i] == '\n') position->line++;
      else if (buffer[i] != ' ' && buffer[i] != '\t' && buffer[i] != '\r')
        command_line = position->line;
    }
    position->offset += count;
  }
  return command_line;
}

static int compare_file_names(const void *left, const void *right) {
  const char *const *a = left;
  const char *const *b = right;
  return strcmp(*a, *b);
}

static void free_file_list(char **files, size_t count) {
  for (size_t i = 0; i < count; i++) free(files[i]);
  free(files);
}

static int list_config_files(const char *input_dir, char ***files_out, size_t *count_out) {
  DIR *directory = opendir(input_dir);
  if (directory == NULL) return 1;

  char **files = NULL;
  size_t count = 0;
  size_t capacity = 0;
  int result = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    size_t name_length = strlen(entry->d_name);
    if (name_length < 5 || strcmp(entry->d_name + name_length - 5, ".conf") != 0) continue;

    char path[MAX_PATH_SIZE];
    int written = snprintf(path, sizeof(path), "%s/%s", input_dir, entry->d_name);
    struct stat st;
    if (written < 0 || (size_t)written >= sizeof(path) || stat(path, &st) != 0 ||
        !S_ISREG(st.st_mode)) continue;

    if (count == capacity) {
      size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
      if (new_capacity < capacity || new_capacity > SIZE_MAX / sizeof(char *)) {
        result = 1;
        break;
      }
      char **grown = realloc(files, new_capacity * sizeof(char *));
      if (grown == NULL) {
        result = 1;
        break;
      }
      files = grown;
      capacity = new_capacity;
    }
    files[count] = strdup(path);
    if (files[count] == NULL) {
      result = 1;
      break;
    }
    count++;
  }

  if (closedir(directory) != 0) result = 1;
  if (result != 0) {
    free_file_list(files, count);
    return 1;
  }

  qsort(files, count, sizeof(char *), compare_file_names);
  *files_out = files;
  *count_out = count;
  return 0;
}

static int normalize_vm_type_paths(VMType *type) {
  char input_path[MAX_PATH_SIZE];
  char executable_path[MAX_PATH_SIZE];
  if (absolute_path(type->input_folder, input_path, sizeof(input_path)) != 0 ||
      absolute_path(type->exec_path, executable_path, sizeof(executable_path)) != 0) return 1;
  strcpy(type->input_folder, input_path);
  strcpy(type->exec_path, executable_path);
  return 0;
}

static int process_config_file(AppContext *context, const char *file_path) {
  int fd = open(file_path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Cannot open configuration file %s: %s\n", file_path, strerror(errno));
    return 1;
  }

  int result = 0;
  ConfigPosition position = {.offset = 0, .line = 1};
  for (;;) {
    Command command = get_next_command(fd);
    if (command == EOC) break;
    size_t command_line = config_command_line(fd, &position);
    switch (command) {
      case CMD_DEFINE: {
        VMType type;
        if (parse_define(fd, &type) != 0 || normalize_vm_type_paths(&type) != 0) {
          fprintf(stderr, "Invalid D command in %s.\n", file_path);
          result = 1;
          break;
        }
        if (datacenter_define_VM(context->dc, &type) != 0) {
          fprintf(stderr, "Could not define VM type %s from %s.\n", type.id, file_path);
          result = 1;
        }
        break;
      }
      case CMD_RESERVE: {
        Reservation reservation = {0};
        if (parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS) == 0) {
          fprintf(stderr, "Invalid R command in %s.\n", file_path);
          result = 1;
          break;
        }
        if (datacenter_reserve(context->dc, &reservation) != 0) {
          fprintf(stderr, "Could not create reservation %s from %s.\n", reservation.id, file_path);
          result = 1;
        }
        break;
      }
      case CMD_EXECUTE: {
        char id[MAX_STRING_SIZE];
        if (parse_execute(fd, id) != 0) {
          fprintf(stderr, "Invalid A command in %s.\n", file_path);
          result = 1;
          break;
        }
        int execution_result = datacenter_execute(context->dc, id);
        if (execution_result == DATACENTER_EXECUTE_CPULIMIT_MISSING) {
          fprintf(stderr, "%s:%zu: cpulimit is required to execute VMs; "
                  "install it and add it to PATH.\n", file_path, command_line);
          result = 1;
        } else if (execution_result != 0) {
          fprintf(stderr, "Could not start reservation %s from %s.\n", id, file_path);
          result = 1;
        } else {
          printf("Started reservation %s (configuration %s).\n", id, file_path);
        }
        break;
      }
      case CMD_LIST:
        if (datacenter_list(context->dc) != 0) result = 1;
        break;
      case CMD_WAIT: {
        unsigned int delay;
        if (parse_wait(fd, &delay) != 0) {
          fprintf(stderr, "Invalid E command in %s.\n", file_path);
          result = 1;
        } else {
          datacenter_wait(delay);
        }
        break;
      }
      case CMD_HELP:
        printf("Commands: D <type> <input-dir> <executable> <ram-GB> <disk-GB> <vCPU>; "
               "R <reservation> [<type> <count> <server>]+; A <reservation>; L; E <ms>.\n");
        break;
      case CMD_EMPTY:
        break;
      case CMD_INVALID:
        fprintf(stderr, "Invalid command in %s.\n", file_path);
        result = 1;
        break;
      case EOC:
        break;
    }
  }
  if (close(fd) != 0) result = 1;
  return result;
}

static void *worker_main(void *argument) {
  AppContext *context = argument;
  for (;;) {
    (void)pthread_mutex_lock(&context->queue_mutex);
    if (context->next_file >= context->file_count) {
      (void)pthread_mutex_unlock(&context->queue_mutex);
      break;
    }
    const char *file_path = context->files[context->next_file++];
    (void)pthread_mutex_unlock(&context->queue_mutex);
    if (process_config_file(context, file_path) != 0)
      atomic_store(&context->had_error, 1);
  }
  (void)atomic_fetch_add(&context->workers_finished, 1);
  return NULL;
}

static void *reaper_main(void *argument) {
  AppContext *context = argument;
  for (;;) {
    int status;
    pid_t child;
    do {
      child = waitpid(-1, &status, WNOHANG);
      if (child > 0 && datacenter_reap_child(context->dc, child, status) &&
          (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
        atomic_store(&context->had_error, 1);
    } while (child > 0 || (child < 0 && errno == EINTR));

    if (atomic_load(&context->workers_finished) == context->worker_count &&
        datacenter_running_vms(context->dc) == 0) break;

    struct timespec pause = {0, 10000000L};
    (void)nanosleep(&pause, NULL);
  }
  return NULL;
}

static void reap_remaining_children(AppContext *context) {
  int status;
  for (;;) {
    pid_t child = waitpid(-1, &status, 0);
    if (child > 0) {
      if (datacenter_reap_child(context->dc, child, status) &&
          (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
        atomic_store(&context->had_error, 1);
      continue;
    }
    if (child < 0 && errno == EINTR) continue;
    break;
  }
}

int main(int argc, char **argv) {
  (void)setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc != 7) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <input_dir> <max_threads>\n", argv[0]);
    return 1;
  }

  size_t servers;
  size_t ram;
  size_t disk;
  size_t max_threads;
  double cpu;
  char input_dir[MAX_PATH_SIZE];
  if (parse_size_t_arg(argv[1], &servers) != 0 ||
      parse_size_t_arg(argv[2], &ram) != 0 ||
      parse_size_t_arg(argv[3], &disk) != 0 ||
      parse_double_arg(argv[4], &cpu) != 0 ||
      parse_size_t_arg(argv[6], &max_threads) != 0 ||
      max_threads == 0 || absolute_path(argv[5], input_dir, sizeof(input_dir)) != 0) {
    fprintf(stderr, "Invalid command line arguments.\n");
    return 1;
  }

  char **files = NULL;
  size_t file_count = 0;
  if (list_config_files(input_dir, &files, &file_count) != 0) {
    fprintf(stderr, "Cannot read configuration directory %s.\n", input_dir);
    return 1;
  }

  DataCenter dc;
  datacenter_init(&dc);
  if (!dc.mutex_initialized) {
    fprintf(stderr, "Failed to initialize data-center synchronization.\n");
    free_file_list(files, file_count);
    return 1;
  }
  Resources resources = {.ram = ram, .disk = disk, .cpu = cpu};
  if (datacenter_configure(&dc, servers, &resources) != 0) {
    datacenter_destroy(&dc);
    free_file_list(files, file_count);
    return 1;
  }

  AppContext context = {
    .dc = &dc,
    .files = files,
    .file_count = file_count,
    .next_file = 0,
    .worker_count = 1,
  };
  if (pthread_mutex_init(&context.queue_mutex, NULL) != 0) {
    datacenter_destroy(&dc);
    free_file_list(files, file_count);
    return 1;
  }
  atomic_init(&context.workers_finished, 0);
  atomic_init(&context.had_error, 0);

  size_t worker_limit = max_threads;
  if (file_count > 0 && worker_limit > file_count) worker_limit = file_count;
  if (worker_limit == 0) worker_limit = 1;
  size_t extra_limit = worker_limit - 1;
  pthread_t *workers = extra_limit == 0 ? NULL : calloc(extra_limit, sizeof(pthread_t));
  if (extra_limit > 0 && workers == NULL) {
    (void)pthread_mutex_destroy(&context.queue_mutex);
    datacenter_destroy(&dc);
    free_file_list(files, file_count);
    return 1;
  }

  size_t extra_created = 0;
  while (extra_created < extra_limit) {
    if (pthread_create(&workers[extra_created], NULL, worker_main, &context) != 0) {
      fprintf(stderr, "Could not create all worker threads; continuing with %zu workers.\n",
              extra_created + 1);
      break;
    }
    extra_created++;
  }
  context.worker_count = extra_created + 1;

  pthread_t reaper;
  int reaper_started = pthread_create(&reaper, NULL, reaper_main, &context) == 0;
  if (!reaper_started)
    fprintf(stderr, "Could not create the VM reaper thread; using the main thread to reap VMs.\n");

  (void)worker_main(&context);
  for (size_t i = 0; i < extra_created; i++) (void)pthread_join(workers[i], NULL);
  if (reaper_started) (void)pthread_join(reaper, NULL);
  else reap_remaining_children(&context);

  free(workers);
  (void)pthread_mutex_destroy(&context.queue_mutex);
  datacenter_destroy(&dc);
  free_file_list(files, file_count);
  return atomic_load(&context.had_error) ? 1 : 0;
}
