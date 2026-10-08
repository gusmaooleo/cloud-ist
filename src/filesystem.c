#define _XOPEN_SOURCE 700

#include "filesystem.h"
#include "constants.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>

static int join_path(const char *directory, const char *name, char *out, size_t out_size) {
  int written = snprintf(out, out_size, "%s/%s", directory, name);
  return written < 0 || (size_t)written >= out_size ? 1 : 0;
}

int make_directories(const char *path) {
  char buffer[MAX_PATH_SIZE];
  size_t length = strlen(path);
  if (length == 0 || length >= sizeof(buffer)) return 1;
  memcpy(buffer, path, length + 1);

  for (size_t i = 1; i <= length; i++) {
    if (buffer[i] != '/' && buffer[i] != '\0') continue;
    char saved = buffer[i];
    buffer[i] = '\0';
    if (buffer[0] != '\0' && mkdir(buffer, 0777) != 0 && errno != EEXIST) return 1;
    if (buffer[0] != '\0') {
      struct stat st;
      if (stat(buffer, &st) != 0 || !S_ISDIR(st.st_mode)) return 1;
    }
    buffer[i] = saved;
  }
  return 0;
}

static int write_all(int fd, const unsigned char *buffer, size_t length) {
  size_t written = 0;
  while (written < length) {
    ssize_t amount = write(fd, buffer + written, length - written);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0) return 1;
    written += (size_t)amount;
  }
  return 0;
}

static int copy_regular_file(const char *source, const char *destination, mode_t mode) {
  int input = open(source, O_RDONLY);
  if (input < 0) return 1;
  int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
  if (output < 0) {
    close(input);
    return 1;
  }

  unsigned char buffer[16384];
  int result = 0;
  for (;;) {
    ssize_t amount = read(input, buffer, sizeof(buffer));
    if (amount < 0 && errno == EINTR) continue;
    if (amount < 0) {
      result = 1;
      break;
    }
    if (amount == 0) break;
    if (write_all(output, buffer, (size_t)amount) != 0) {
      result = 1;
      break;
    }
  }
  if (fchmod(output, mode & 0777) != 0) result = 1;
  if (close(input) != 0) result = 1;
  if (close(output) != 0) result = 1;
  return result;
}

static int copy_directory_contents_inner(const char *source_dir, const char *destination_dir) {
  DIR *directory = opendir(source_dir);
  if (directory == NULL) return 1;

  int result = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

    char source[MAX_PATH_SIZE];
    char destination[MAX_PATH_SIZE];
    struct stat st;
    if (join_path(source_dir, entry->d_name, source, sizeof(source)) != 0 ||
        join_path(destination_dir, entry->d_name, destination, sizeof(destination)) != 0 ||
        lstat(source, &st) != 0) {
      result = 1;
      break;
    }

    if (S_ISDIR(st.st_mode)) {
      if (mkdir(destination, st.st_mode & 0777) != 0 ||
          copy_directory_contents_inner(source, destination) != 0 ||
          chmod(destination, st.st_mode & 0777) != 0) {
        result = 1;
        break;
      }
    } else if (S_ISREG(st.st_mode)) {
      if (copy_regular_file(source, destination, st.st_mode) != 0) {
        result = 1;
        break;
      }
    } else if (S_ISLNK(st.st_mode)) {
      char link_target[MAX_PATH_SIZE];
      ssize_t link_length = readlink(source, link_target, sizeof(link_target) - 1);
      if (link_length < 0 || (size_t)link_length >= sizeof(link_target) - 1) {
        result = 1;
        break;
      }
      link_target[link_length] = '\0';
      if (symlink(link_target, destination) != 0) {
        result = 1;
        break;
      }
    } else {
      result = 1;
      break;
    }
  }

  if (closedir(directory) != 0) result = 1;
  return result;
}

int copy_directory_contents(const char *source_dir, const char *destination_dir) {
  struct stat st;
  if (stat(source_dir, &st) != 0 || !S_ISDIR(st.st_mode) ||
      stat(destination_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 1;
  return copy_directory_contents_inner(source_dir, destination_dir);
}

int remove_path_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : 1;
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return unlink(path) == 0 ? 0 : 1;

  /* A workload may leave its copied input directories without owner access. */
  if (chmod(path, st.st_mode | S_IRUSR | S_IWUSR | S_IXUSR) != 0) return 1;

  DIR *directory = opendir(path);
  if (directory == NULL) return 1;
  int result = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char child[MAX_PATH_SIZE];
    if (join_path(path, entry->d_name, child, sizeof(child)) != 0 ||
        remove_path_tree(child) != 0) {
      result = 1;
      break;
    }
  }
  if (closedir(directory) != 0) result = 1;
  if (result == 0 && rmdir(path) != 0) result = 1;
  return result;
}

int path_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISREG(st.st_mode);
}

int executable_in_path(const char *name) {
  const char *search_path = getenv("PATH");
  if (search_path == NULL) search_path = "/usr/bin:/bin";

  for (const char *entry = search_path;;) {
    const char *separator = strchr(entry, ':');
    size_t length = separator == NULL ? strlen(entry) : (size_t)(separator - entry);
    if (length < MAX_PATH_SIZE) {
      char candidate[MAX_PATH_SIZE];
      int written = length == 0
        ? snprintf(candidate, sizeof(candidate), "%s", name)
        : snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, entry, name);
      if (written >= 0 && (size_t)written < sizeof(candidate) &&
          file_exists(candidate) && access(candidate, X_OK) == 0) return 1;
    }
    if (separator == NULL) break;
    entry = separator + 1;
  }
  return 0;
}

int absolute_path(const char *path, char *buffer, size_t size){
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size) {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}
