# Tests of the emscripten-independent helpers behind MansouraHTMLCanvas
# (native), and of MansouraHTMLCanvas itself (needs emsdk and node on the path)
CXX?=g++
CXXFLAGS+=-std=c++17 -Wall -Werror -I../src
EMXX?=em++

all: htmlCanvasHelpers recordingContext
	./htmlCanvasHelpers
	./recordingContext

htmlCanvasHelpers: htmlCanvasHelpers.cc ../src/textMarkup.cc ../src/canvasPath.h ../src/textMarkup.h
	$(CXX) $(CXXFLAGS) htmlCanvasHelpers.cc ../src/textMarkup.cc -o $@

recordingContext: recordingContext.cc ../src/mansouraRecording.cc ../src/mansouraRecording.h ../src/mansouraNoCairo.cc ../src/canvasPath.h ../src/textMarkup.cc
	$(CXX) $(CXXFLAGS) recordingContext.cc ../src/mansouraRecording.cc ../src/mansouraNoCairo.cc ../src/textMarkup.cc -o $@

# run the wasm build against a mock CanvasRenderingContext2D
wasm: htmlCanvasHarness.js
	node htmlCanvas.test.js

htmlCanvasHarness.js: htmlCanvasHarness.cc ../src/mansouraHTMLCanvas.cc ../src/mansouraHTMLCanvas.h ../src/canvasPath.h ../src/textMarkup.cc ../src/textMarkup.h ../src/mansouraRecording.cc ../src/mansouraRecording.h ../src/mansouraNoCairo.cc
	$(EMXX) -std=c++20 -fwasm-exceptions -Wall -I../src htmlCanvasHarness.cc ../src/mansouraHTMLCanvas.cc ../src/textMarkup.cc \
	  ../src/mansouraRecording.cc ../src/mansouraNoCairo.cc \
	  --bind -sMODULARIZE -sEXPORT_NAME=Mod -o $@

clean:
	rm -f htmlCanvasHelpers recordingContext htmlCanvasHarness.js htmlCanvasHarness.wasm

.PHONY: all wasm clean
