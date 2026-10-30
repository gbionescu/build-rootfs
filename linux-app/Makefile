SUBDIRS := process-fork pthread-sum pipe-ipc shm-sem signal-demo epoll-server mmap-file ebpf kernel-perf kernel-trace
GUI_DIR := stopwatch-qt

all:
	@for dir in $(SUBDIRS); do \
		echo "==> $$dir"; \
		$(MAKE) -C $$dir || exit 1; \
	done

test:
	./run-all.sh

test-perf:
	./kernel-perf/run_perf.sh all

test-trace:
	./kernel-trace/trace_demo.sh all

test-ebpf:
	./ebpf/run_ebpf.sh all

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

.PHONY: all test test-perf test-trace test-ebpf gui modules modules-clean clean
