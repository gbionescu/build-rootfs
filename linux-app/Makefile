SUBDIRS := process-fork pthread-sum pipe-ipc shm-sem signal-demo epoll-server mmap-file

all:
	@for dir in $(SUBDIRS); do \
		echo "==> $$dir"; \
		$(MAKE) -C $$dir || exit 1; \
	done

test:
	./run-all.sh

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

.PHONY: all test clean
