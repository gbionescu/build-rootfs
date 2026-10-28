SUBDIRS := process-fork pthread-sum pipe-ipc shm-sem signal-demo epoll-server mmap-file kernel-perf kernel-trace
GUI_DIR := stopwatch-qt

all:
	@for dir in $(SUBDIRS); do \
		echo "==> $$dir"; \
		$(MAKE) -C $$dir || exit 1; \
	done

test:
	./run-all.sh

gui:
	$(MAKE) -C $(GUI_DIR)

modules:
	./kernel-drivers/build.sh

modules-clean:
	./kernel-drivers/build.sh clean

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	$(MAKE) -C $(GUI_DIR) clean

.PHONY: all test gui modules modules-clean clean
