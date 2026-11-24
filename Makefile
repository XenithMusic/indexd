CC = clang++
LD = clang++
ELEV = sudo

CFLAGS =
LDFLAGS = -lsqlite3 

SRCS = $(shell find src/*.cpp)
OBJS = $(patsubst src/%.cpp,build/%.o,$(SRCS))

OUT = build/indexd

build/%.o: src/%.cpp
	$(CC) -c $< -o $@ $(CFLAGS)

$(OUT): $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS)

run: $(OUT)
	$(ELEV) $(OUT)