SRCS = $(wildcard *.cc)
OBJS = $(SRCS:%.cc=out/%.o)

# If you want to compile aus for debugging, invoke make as
# `make CXXFLAGS=-g`.
CFLAGS = -O2
CXXFLAGS = -O2

AUS_CXXFLAGS := -std=c++20 -fno-exceptions -fno-rtti

DEPFLAGS = -MF out/$*.d -MMD -MP -MT $@

all: aus

-include $(SRCS:%.cc=out/%.d)

aus: $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

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
