# Host-side tests (run on your PC / in CI, not on the calculator).
#
#   make -C test test                                   # unit tests + bot runs + progression
#   make -C test bot BOT_ARGS="--veh 0 --map 0"         # bot drives one vehicle / map, prints stats
#   make -C test career                                 # bot plays a whole career (balance check)
#   make -C test SDK_DIR=/path/to/hollyhock-3/sdk shots # run the real UI headlessly -> test/out/*.png
#   make -C test SDK_DIR=... perf                       # time the renderer on the PC (relative numbers)
#
SDK_DIR ?= /sdk
CXX     ?= g++
OUT     ?= out
STD     := -std=gnu++17
WARN    := -Wall -Wextra

GAME_SRC := ../game.cpp
APP_SRC  := host_app.cpp ../main.cpp ../gfx.cpp ../render.cpp ../save.cpp $(GAME_SRC)
APP_FLAGS := -O2 $(WARN) $(STD) -DHC_HOST -Dmain=hc_main -I$(SDK_DIR)/include -I..
HDRS := $(wildcard ../*.h)
BOT_ARGS ?=

.PHONY: test unit bot career shots film clean

test: unit bot career

unit: $(OUT)/t_game
	$(OUT)/t_game

# Drives every vehicle on every map with human-like inputs; fails on broken physics.
bot: $(OUT)/bot
	$(OUT)/bot $(BOT_ARGS)

# A whole career: run, bank coins, buy upgrades / vehicles / maps, repeat.
career: $(OUT)/bot
	$(OUT)/bot --career

$(OUT)/t_game: test_game.cpp $(GAME_SRC) ../save.cpp $(HDRS) | $(OUT)
	$(CXX) -O2 $(WARN) $(STD) -DHC_HOST -DHC_SAVE_PATH='"out/test.sav"' -I.. -o $@ test_game.cpp $(GAME_SRC) ../save.cpp

$(OUT)/bot: bot.cpp bot.h $(GAME_SRC) $(HDRS) | $(OUT)
	$(CXX) -O2 $(WARN) $(STD) -I.. -o $@ bot.cpp $(GAME_SRC)

$(OUT)/app: $(APP_SRC) $(HDRS) | $(OUT)
	$(CXX) $(APP_FLAGS) -o $@ $(APP_SRC)

shots: $(OUT)/app
	rm -f $(OUT)/hillclimber.sav $(OUT)/*.ppm
	$(OUT)/app script.txt
	python3 ppm2png.py $(OUT)

# Renders frames of a bot run on every map (needs the SDK headers for gfx.cpp).
$(OUT)/film: film.cpp bot.h ../gfx.cpp ../render.cpp $(GAME_SRC) $(HDRS) | $(OUT)
	$(CXX) -O2 $(WARN) $(STD) -DHC_HOST -I$(SDK_DIR)/include -I.. -o $@ film.cpp ../gfx.cpp ../render.cpp $(GAME_SRC)

film: $(OUT)/film
	rm -f $(OUT)/film_*.ppm
	$(OUT)/film
	python3 ppm2png.py $(OUT)

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -f $(OUT)/*
