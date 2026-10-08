#include "datacenter.h"
#include "datacenter_utils.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <math.h>

void datacenter_init(DataCenter *dc) {
  dc->servers = NULL;
  dc->num_servers = 0;

  dc->num_vm_types = 0;

  dc->num_reservations = 0;

  dc->configured = 0;
  dc->mutex_initialized = pthread_mutex_init(&dc->mutex, NULL) == 0;
}

void datacenter_destroy(DataCenter *dc) {
  for (size_t i = 0; i < dc->num_servers; i++) {
    for(size_t j = 0; j < dc->servers[i].num_hosted_vms; j++){
      free(dc->servers[i].hosted_vms[j]);
    }
  }
  free(dc->servers);
  dc->servers = NULL;
  if (dc->mutex_initialized) {
    (void)pthread_mutex_destroy(&dc->mutex);
    dc->mutex_initialized = 0;
  }
}

int datacenter_configure(DataCenter *dc, size_t num_servers, Resources *resources){
  if (!dc->mutex_initialized) return 1;
  (void)pthread_mutex_lock(&dc->mutex);
  if(dc->configured){
    fprintf(stderr, "Data Center is already configured!\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if (num_servers == 0 || resources->ram == 0 || resources->disk == 0 || resources->cpu <= 0.0) {
    fprintf(stderr, "Invalid configuration values.\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  dc->servers = (Server*)malloc(num_servers*sizeof(Server));

  if(dc->servers == NULL){
    fprintf(stderr, "Failed to allocate memory for servers.\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  for (size_t i = 0; i < num_servers; i++) {
    dc->servers[i].id = i+1;
    dc->servers[i].total = *resources;
    dc->servers[i].available = *resources;
    dc->servers[i].num_hosted_vms = 0;
  }

  dc->num_servers = num_servers;
  dc->configured = 1;

  (void)pthread_mutex_unlock(&dc->mutex);
  return 0;
}

int datacenter_define_VM(DataCenter *dc, VMType *type){
  (void)pthread_mutex_lock(&dc->mutex);
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if(VMType_exists(dc, type->id) != NULL){
    fprintf(stderr, "VM type with id %s already exists.\n", type->id);
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if (!file_exists(type->exec_path)) {
    fprintf(stderr, "Invalid executable path.\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if (!path_exists(type->input_folder)) {
    fprintf(stderr, "Invalid input folder.\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if(dc->num_vm_types >= MAX_VM_TYPES){
    fprintf(stderr, "Maximum of VM types reached.\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }
  
  dc->vm_types[dc->num_vm_types++] = *type;

  (void)pthread_mutex_unlock(&dc->mutex);
  return 0;
}

int datacenter_reserve(DataCenter *dc, Reservation *reservation) {
  (void)pthread_mutex_lock(&dc->mutex);
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if(reservation_validate(dc, reservation) != 0){
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if (dc->num_reservations >= MAX_RESERVATIONS){
    fprintf(stderr, "Max number of reservations reached\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  if (reservation_commit(dc, reservation) != 0) {
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  dc->reservations[dc->num_reservations] = *reservation;
  dc->num_reservations++;

  (void)pthread_mutex_unlock(&dc->mutex);
  return 0;
}

static Reservation *find_reservation(DataCenter *dc, const char *reservation_id) {
  for (size_t i = 0; i < dc->num_reservations; i++) {
    if (strcmp(dc->reservations[i].id, reservation_id) == 0) return &dc->reservations[i];
  }
  return NULL;
}

static int vm_directory(const char *reservation_id, const char *vm_id, char *path, size_t size) {
  int written = snprintf(path, size, "/tmp/CloudIST/%s/%s", reservation_id, vm_id);
  return written < 0 || (size_t)written >= size ? 1 : 0;
}

static int reservation_directory(const char *reservation_id, char *path, size_t size) {
  int written = snprintf(path, size, "/tmp/CloudIST/%s", reservation_id);
  return written < 0 || (size_t)written >= size ? 1 : 0;
}

static void remove_vm_locked(DataCenter *dc, Reservation *reservation, size_t vm_index,
                             int remove_directory) {
  VM *vm = reservation->vms[vm_index];
  char folder[MAX_PATH_SIZE];
  if (remove_directory && vm_directory(reservation->id, vm->id, folder, sizeof(folder)) == 0)
    (void)remove_path_tree(folder);

  resources_add(&vm->server->available, vm->type->required);
  for (size_t i = 0; i < vm->server->num_hosted_vms; i++) {
    if (vm->server->hosted_vms[i] == vm) {
      memmove(&vm->server->hosted_vms[i], &vm->server->hosted_vms[i + 1],
              (vm->server->num_hosted_vms - i - 1) * sizeof(VM *));
      vm->server->num_hosted_vms--;
      vm->server->hosted_vms[vm->server->num_hosted_vms] = NULL;
      break;
    }
  }

  memmove(&reservation->vms[vm_index], &reservation->vms[vm_index + 1],
          (reservation->num_vms - vm_index - 1) * sizeof(VM *));
  reservation->num_vms--;
  reservation->vms[reservation->num_vms] = NULL;
  free(vm);

  if (reservation->num_vms == 0) {
    char directory[MAX_PATH_SIZE];
    if (reservation_directory(reservation->id, directory, sizeof(directory)) == 0)
      (void)remove_path_tree(directory);
    size_t index = (size_t)(reservation - dc->reservations);
    memmove(&dc->reservations[index], &dc->reservations[index + 1],
            (dc->num_reservations - index - 1) * sizeof(Reservation));
    dc->num_reservations--;
  }
}

int datacenter_execute(DataCenter *dc, const char *reservation_id) {
  VM *vms[MAX_RESERVATION_VMS];
  char folders[MAX_RESERVATION_VMS][MAX_PATH_SIZE] = {{0}};
  char source_dirs[MAX_RESERVATION_VMS][MAX_PATH_SIZE] = {{0}};
  size_t vm_count = 0;

  (void)pthread_mutex_lock(&dc->mutex);
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }
  Reservation *reservation = find_pending_reservation(dc, reservation_id);
  if (reservation == NULL) {
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }
  if (!executable_in_path("cpulimit")) {
    (void)pthread_mutex_unlock(&dc->mutex);
    return DATACENTER_EXECUTE_CPULIMIT_MISSING;
  }
  reservation->state = RES_STATE_STARTING;
  vm_count = reservation->num_vms;
  int path_error = 0;
  for (size_t i = 0; i < vm_count; i++) {
    vms[i] = reservation->vms[i];
    if (vm_directory(reservation_id, vms[i]->id, folders[i], sizeof(folders[i])) != 0 ||
        strlen(vms[i]->type->input_folder) >= sizeof(source_dirs[i])) {
      path_error = 1;
      break;
    }
    strcpy(source_dirs[i], vms[i]->type->input_folder);
  }
  (void)pthread_mutex_unlock(&dc->mutex);

  int stage_error = path_error;
  for (size_t i = 0; i < vm_count && stage_error == 0; i++) {
    if (remove_path_tree(folders[i]) != 0 || make_directories(folders[i]) != 0 ||
        copy_directory_contents(source_dirs[i], folders[i]) != 0) {
      fprintf(stderr, "Failed to copy VM input files for %s.\n", vms[i]->id);
      stage_error = 1;
    }
  }
  if (stage_error != 0) {
    for (size_t i = 0; i < vm_count; i++) (void)remove_path_tree(folders[i]);
    (void)pthread_mutex_lock(&dc->mutex);
    reservation = find_reservation(dc, reservation_id);
    if (reservation != NULL && reservation->state == RES_STATE_STARTING)
      reservation->state = RES_STATE_PENDING;
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  (void)pthread_mutex_lock(&dc->mutex);
  reservation = find_reservation(dc, reservation_id);
  if (reservation == NULL || reservation->state != RES_STATE_STARTING ||
      reservation->num_vms != vm_count) {
    (void)pthread_mutex_unlock(&dc->mutex);
    for (size_t i = 0; i < vm_count; i++) (void)remove_path_tree(folders[i]);
    return 1;
  }

  size_t started = 0;
  for (size_t i = 0; i < vm_count; i++) {
    double exact_cpu_percent = vms[i]->type->required.cpu / dc->servers[0].total.cpu *
                               (double)dc->num_servers * 100.0;
    /* cpulimit parses -l as an integer; round the computed percentage. */
    int cpu_percent;
    if (exact_cpu_percent >= (double)INT_MAX) {
      cpu_percent = INT_MAX;
    } else {
      long rounded_percent = lround(exact_cpu_percent);
      if (rounded_percent < 1) rounded_percent = 1;
      cpu_percent = (int)rounded_percent;
    }
    pid_t child = fork();
    if (child == 0) {
      spawn_vm_child(vms[i], folders[i], cpu_percent);
      _exit(127);
    }
    if (child < 0) {
      fprintf(stderr, "Failed to fork VM %s: %s\n", vms[i]->id, strerror(errno));
      break;
    }
    vms[i]->pid = child;
    vms[i]->state = VM_STATE_RUNNING;
    started++;
  }

  if (started == 0) {
    reservation->state = RES_STATE_PENDING;
    for (size_t i = 0; i < vm_count; i++) {
      vms[i]->pid = 0;
      vms[i]->state = VM_STATE_RESERVED;
    }
    (void)pthread_mutex_unlock(&dc->mutex);
    for (size_t i = 0; i < vm_count; i++) (void)remove_path_tree(folders[i]);
    return 1;
  }

  reservation->state = RES_STATE_RUNNING;
  if (started < vm_count) {
    for (size_t i = vm_count; i > started; i--) {
      for (size_t j = 0; j < reservation->num_vms; j++) {
        if (reservation->vms[j] == vms[i - 1]) {
          remove_vm_locked(dc, reservation, j, 1);
          break;
        }
      }
    }
  }
  (void)pthread_mutex_unlock(&dc->mutex);
  return started == vm_count ? 0 : 1;
}

int datacenter_list(DataCenter *dc) {
  (void)pthread_mutex_lock(&dc->mutex);
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    (void)pthread_mutex_unlock(&dc->mutex);
    return 1;
  }

  printf("=================================== DATA CENTER STATUS ===================================\n");
  printf("\n[ PHYSICAL SERVERS ] (%zu total)\n", dc->num_servers);
  printf("------------------------------------------------------------------------------------------\n");

  for (size_t i = 0; i < dc->num_servers; i++) {
    Server server = dc->servers[i];

    Resources used = server.total;
    resources_sub(&used, server.available);

    printf(" Server #%zu\n", server.id);
    printf("   ├─ Resources Total     : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server.total.cpu, server.total.ram, server.total.disk);
    printf("   ├─ Resources Used      : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            used.cpu, used.ram, used.disk);
    printf("   ├─ Resources Available : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server.available.cpu, server.available.ram, server.available.disk);
    printf("   └─ Hosted VMs (%zu):\n", server.num_hosted_vms);

    if (server.num_hosted_vms == 0) {
      printf("        (None)\n");
    } else {
      for (size_t j = 0; j < server.num_hosted_vms; j++) {
        VM *vm = server.hosted_vms[j];
        const char *prefix = (j == server.num_hosted_vms - 1) ? "└──" : "├──";

        printf("        %s VM ID : %-16s | Type: %-10s | State: %-10s",
                prefix,
                vm->id,
                vm->type ? vm->type->id : "Unknown",
                vm_state_to_string(vm->state));

        if (vm->state == VM_STATE_RUNNING) {
          printf(" | PID: %d", (int)vm->pid);
        }
        printf("\n");
      }
    }
    printf("\n");
  }

  printf("[ DEFINED VM TYPES ] (%zu registered)\n", dc->num_vm_types);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_vm_types == 0) {
    printf(" (None)\n\n");
  } else {
      for (size_t i = 0; i < dc->num_vm_types; i++) {
        VMType vt = dc->vm_types[i];
        printf("  * Type ID: %-12s | Requirements: %.2f vCPU, %zu GB RAM, %zu GB Disk\n",
                vt.id, vt.required.cpu, vt.required.ram, vt.required.disk);
        printf("    ├─ Executable : %s\n", vt.exec_path);
        printf("    └─ Input Dir  : %s\n\n", vt.input_folder);
      }
      printf("\n");
  }

  printf("[ RESERVATIONS ] (%zu total)\n", dc->num_reservations);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_reservations == 0) {
    printf(" (None)\n\n");
  } else {
    for (size_t i = 0; i < dc->num_reservations; i++) {
      Reservation res = dc->reservations[i];
      printf("  * Reservation ID: %-12s | State: %-8s | Total VMs: %zu\n",
              res.id, res_state_to_string(res.state), res.num_vms);

      printf("    ├─ Requested Pairs:\n");
      for (size_t k = 0; k < res.items_count; k++) {
        printf("    │    • %-12s : %zu unit(s) on server #%zu\n",
               res.items[k].vm_type_id, res.items[k].amount, res.items[k].server_id);
      }

      printf("    └─ Allocated VMs:\n");
      if (res.num_vms == 0) {
        printf("         (None)\n");
      } else {
          for (size_t k = 0; k < res.num_vms; k++) {
            VM *vm = res.vms[k];
            const char *prefix = (k == res.num_vms - 1) ? "└──" : "├──";
            printf("         %s ID: %-15s | Host Server: #%zu\n",
                    prefix,
                    vm->id,
                    vm->server->id);
          }
      }
      printf("\n");
    }
  }

  printf("================================================================--------------------------\n\n");
  (void)pthread_mutex_unlock(&dc->mutex);
  return 0;
}

int datacenter_reap_child(DataCenter *dc, pid_t pid, int status) {
  int found = 0;
  (void)pthread_mutex_lock(&dc->mutex);
  for (size_t i = 0; i < dc->num_reservations && !found; i++) {
    Reservation *reservation = &dc->reservations[i];
    for (size_t j = 0; j < reservation->num_vms; j++) {
      VM *vm = reservation->vms[j];
      if (vm->pid != pid) continue;
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fprintf(stderr, "VM %s (PID %d) exited with an error.\n", vm->id, (int)pid);
      remove_vm_locked(dc, reservation, j, 1);
      found = 1;
      break;
    }
  }
  (void)pthread_mutex_unlock(&dc->mutex);
  return found;
}

size_t datacenter_running_vms(DataCenter *dc) {
  size_t running = 0;
  (void)pthread_mutex_lock(&dc->mutex);
  for (size_t i = 0; i < dc->num_reservations; i++) {
    for (size_t j = 0; j < dc->reservations[i].num_vms; j++) {
      if (dc->reservations[i].vms[j]->state == VM_STATE_RUNNING) running++;
    }
  }
  (void)pthread_mutex_unlock(&dc->mutex);
  return running;
}

void datacenter_wait(unsigned int delay_ms){
  struct timespec delay = (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}
