SRCS = $(wildcard *.cc)
OBJS = $(SRCS:%.cc=out/%.o)

# If you want to compile aus for debugging, invoke make as
# `make CXXFLAGS=-g`.
CFLAGS = -O2
CXXFLAGS = -O2

AUS_CXXFLAGS := -std=c++20 -fno-exceptions -fno-rtti

DEPFLAGS = -MF out/$*.d -MMD -MP -MT $@

all: aus aus_test

-include $(SRCS:%.cc=out/%.d)

TEST_SRCS = $(wildcard *_test.cc)
TEST_OBJS = $(TEST_SRCS:%.cc=out/%.o)

aus: $(filter-out $(TEST_OBJS),$(OBJS))
	$(CXX) $^ -o $@ $(LDFLAGS)

aus_test: $(filter-out out/aus.o,$(OBJS))
	$(CXX) $^ -o $@ $(LDFLAGS)

out/%.o: %.cc out/.keep
	$(CXX) $(AUS_CXXFLAGS) $(DEPFLAGS) $(CXXFLAGS) -c -o $@ $<

out/.keep:
	mkdir -p $(@D)
	touch $@

clean:
	rm -rf aus out

format:
	clang-format -i *.h *.cc

.PHONY: all clean format
