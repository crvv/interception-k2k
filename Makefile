CFLAGS += -std=c99 -O3 -g -Wall -Wextra -Werror -Wno-type-limits

CONFIG_DIR ?= config

.PHONY: all
all: k2k

k2k: k2k.c $(CONFIG_DIR)/map-rules.h.in $(CONFIG_DIR)/multi-rules.h.in
	$(CC) $(CFLAGS) -I$(CONFIG_DIR) $< -o $@

.PHONY: install
install: k2k
	sudo rm /usr/local/bin/k2k
	sudo mv k2k /usr/local/bin/
	sudo systemctl restart udevmon.service

.PHONY: test
test:
	CFLAGS=-DVERBOSE make
	sudo rm /usr/local/bin/k2k
	sudo mv k2k /usr/local/bin/
	sudo udevmon -c /etc/interception/udevmon.yaml
