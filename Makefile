#target := ps5
CC := clang
CFLAGS := -O2 -Wall -std=gnu17 -pthread
LDFLAGS := -pthread

# If using ps5-payload-sdk
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
ifneq ($(wildcard $(PS5_PAYLOAD_SDK)/Makefile),)
    include $(PS5_PAYLOAD_SDK)/Makefile
endif

TARGET := discord_rpc.elf
SRCS := src/main.c \
        src/config.c \
        src/webserver.c \
        src/title_resolver.c \
        src/kabuki_ui.c \
        src/discord_auth.c \
        src/app_installer.c \
        src/discord.c

OBJS := $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
