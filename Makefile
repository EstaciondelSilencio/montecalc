# MonteCalc build
#   make            build the extension (build/MonteCalc.oxt)
#   make test       run core unit tests
#   make itest      run integration tests in headless LibreOffice
#   make install    install the extension for the current user (unopkg)

LO      ?= /usr/lib/libreoffice
SDK     ?= $(LO)/sdk
TYPES   := $(LO)/program/types.rdb $(LO)/program/types/offapi.rdb
B       := build
GEN     := $(B)/gen
INC     := $(B)/inc
CXX     ?= g++
CXXFLAGS ?= -O2 -g0
CXXFLAGS += -std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter \
            -DLINUX -DUNX -DCPPU_ENV=gcc3 -I/usr/include/libreoffice -I$(SDK)/include -I$(INC) -I$(GEN)
LDLIBS  := -L$(LO)/program -L$(SDK)/lib -luno_cppuhelpergcc3 -luno_cppu -luno_sal
SDKENV  := LD_LIBRARY_PATH=$(LO)/program

CORE_SRC := $(wildcard core/*.cpp)
UNO_SRC  := $(wildcard uno/*.cpp)
CORE_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(CORE_SRC))
UNO_OBJ  := $(patsubst %.cpp,$(B)/obj/%.o,$(UNO_SRC))

.PHONY: all test itest install clean
all: $(B)/MonteCalc.oxt

$(GEN)/XMonteCalc.idl $(GEN)/functions_decl.inc $(GEN)/functions_impl.inc $(GEN)/functions_meta.inc: tools/gen_functions.py
	python3 tools/gen_functions.py $(GEN)

$(B)/montecalc.rdb: $(GEN)/XMonteCalc.idl
	$(SDKENV) $(SDK)/bin/unoidl-write $(TYPES) $< $@

# C++ headers for the office API (generated once) and for our interface
$(INC)/.office: | $(B)
	mkdir -p $(INC)
	$(SDKENV) $(SDK)/bin/cppumaker -Gc -O$(abspath $(INC)) $(TYPES)
	touch $@

$(INC)/.own: $(B)/montecalc.rdb $(INC)/.office
	$(SDKENV) $(SDK)/bin/cppumaker -Gc -O$(abspath $(INC)) -Torg.montecalc.XMonteCalc $(foreach t,$(TYPES),-X$(t)) $(abspath $<)
	touch $@

$(B)/obj/core/%.o: core/%.cpp core/*.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/obj/uno/%.o: uno/%.cpp uno/*.hpp core/*.h $(INC)/.own $(GEN)/functions_impl.inc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/libmontecalc.so: $(CORE_OBJ) $(UNO_OBJ)
	$(CXX) -shared -Wl,--no-undefined -Wl,-z,origin -o $@ $^ $(LDLIBS)

$(B)/MonteCalc.oxt: $(B)/libmontecalc.so $(B)/montecalc.rdb ext/*.xml ext/*.txt ext/montecalc.components ext/registry/*.xcu ext/META-INF/manifest.xml
	rm -rf $(B)/oxt && mkdir -p $(B)/oxt
	cp -r ext/. $(B)/oxt/
	cp $(B)/libmontecalc.so $(B)/montecalc.rdb $(B)/oxt/
	cd $(B)/oxt && rm -f ../MonteCalc.oxt && zip -qr ../MonteCalc.oxt .
	@echo "built $@"

$(B):
	mkdir -p $(B)

$(B)/core_tests: tests/core_tests.cpp $(CORE_SRC) core/*.h | $(B)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -o $@ tests/core_tests.cpp $(CORE_SRC)

test: $(B)/core_tests
	./$(B)/core_tests

itest: $(B)/MonteCalc.oxt
	python3 tests/integration_test.py $(B)/MonteCalc.oxt

install: $(B)/MonteCalc.oxt
	unopkg add --force $(B)/MonteCalc.oxt

clean:
	rm -rf $(B)
