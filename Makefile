# Sugarcane Irrigation Advisory - OS mini project (KJS-AGR-01)
CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-format-truncation -O2 -std=c11 -Iinclude
LDLIBS  = -lm
BIN     = bin

PROGS = $(BIN)/pipeline $(BIN)/acquire $(BIN)/predict $(BIN)/advise \
        $(BIN)/scheduler $(BIN)/sensor_sync $(BIN)/banker

.PHONY: all data model run exp4 exp6 exp7 exp9 test clean distclean

all: $(PROGS)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/common.o: src/common.c include/irrigation.h | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

# Exp 4 - pipeline and the stage programs it fork/execs
$(BIN)/pipeline: src/pipeline.c $(BIN)/common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(BIN)/acquire: src/acquire.c $(BIN)/common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(BIN)/predict: src/predict.c $(BIN)/common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
$(BIN)/advise: src/advise.c $(BIN)/common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
# Exp 6
$(BIN)/scheduler: src/scheduler.c $(BIN)/common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
# Exp 7 (pthreads)
$(BIN)/sensor_sync: src/sensor_sync.c $(BIN)/common.o
	$(CC) $(CFLAGS) -pthread $^ -o $@ $(LDLIBS)
# Exp 9
$(BIN)/banker: src/banker.c
	$(CC) $(CFLAGS) $^ -o $@

data:
	python3 scripts/generate_data.py

model:
	python3 scripts/train_model.py

exp4: all
	./$(BIN)/pipeline
exp6: all
	./$(BIN)/scheduler -a measured -t 1 -q 2,5,10
	./$(BIN)/scheduler -a zero -t 1 -q 2,5,10
exp7: all
	./$(BIN)/sensor_sync
	-./$(BIN)/sensor_sync --unsafe
exp9: all
	./$(BIN)/banker

run: all
	bash scripts/run_all.sh

test: all
	bash tests/run_tests.sh

clean:
	rm -rf $(BIN)

distclean: clean
	rm -rf data/processed/* outputs/* logs/*
