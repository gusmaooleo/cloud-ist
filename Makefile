CC = gcc

SRC_DIR = src
INC_DIR = include
SOURCES = $(SRC_DIR)/parser.c $(SRC_DIR)/datacenter.c \
		  $(SRC_DIR)/datacenter_utils.c $(SRC_DIR)/resources.c \
		  $(SRC_DIR)/filesystem.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(wildcard $(INC_DIR)/*.h)
RUN_ARGS ?= 4 16 500 8 ./configs 3

.PHONY: all run clean format

CFLAGS = -g -std=c17 -D_POSIX_C_SOURCE=200809L \
		 -Wall -Werror -Wextra \
		 -Wcast-align -Wconversion -Wfloat-equal -Wformat=2 -Wnull-dereference -Wshadow -Wsign-conversion -Wswitch-enum -Wundef -Wunreachable-code -Wunused \
		 -fsanitize=undefined -pthread -I$(INC_DIR)

ifneq ($(shell uname -s),Darwin) # if not MacOS
	CFLAGS += -fmax-errors=5
endif

all: cloudIST

cloudIST: $(SRC_DIR)/main.c $(OBJECTS) $(HEADERS)
	$(CC) $(CFLAGS) $(SLEEP) -o cloudIST $(SRC_DIR)/main.c $(OBJECTS) -lm

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

run: cloudIST
	@./cloudIST $(RUN_ARGS)

clean:
	rm -f $(SRC_DIR)/*.o cloudIST
	rm -rf cloudIST.dSYM

format:
	@which clang-format >/dev/null 2>&1 || echo "Please install clang-format to run this command"
	clang-format -i *.c *.h
