CXX ?= clang++
CXXFLAGS = -std=c++17 -O2 -Wall -DGLFW_INCLUDE_NONE -DGL_SILENCE_DEPRECATION \
           -I/opt/homebrew/include -Ithird_party/imgui -Ithird_party/imgui/backends
LDFLAGS = -L/opt/homebrew/lib -lglfw -framework OpenGL -framework Cocoa -framework IOKit

TARGET = graficador

IMGUI_DIR = third_party/imgui
OBJS = main.o parser.o raster.o gui.o project.o \
       imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o \
       imgui_impl_glfw.o imgui_impl_opengl3.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

main.o: main.cpp parser.h raster.h gui.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

parser.o: parser.cpp parser.h
	$(CXX) $(CXXFLAGS) -c parser.cpp -o parser.o

raster.o: raster.cpp raster.h
	$(CXX) $(CXXFLAGS) -c raster.cpp -o raster.o

project.o: project.cpp project.h gui.h parser.h
	$(CXX) $(CXXFLAGS) -c project.cpp -o project.o

gui.o: gui.cpp gui.h project.h $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/backends/imgui_impl_glfw.h $(IMGUI_DIR)/backends/imgui_impl_opengl3.h
	$(CXX) $(CXXFLAGS) -c gui.cpp -o gui.o

imgui.o: $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/imgui.cpp -o imgui.o

imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/imgui_draw.cpp -o imgui_draw.o

imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/imgui_tables.cpp -o imgui_tables.o

imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/imgui_widgets.cpp -o imgui_widgets.o

imgui_impl_glfw.o: $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp -o imgui_impl_glfw.o

imgui_impl_opengl3.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.h
	$(CXX) $(CXXFLAGS) -w -c $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp -o imgui_impl_opengl3.o

clean:
	rm -f $(TARGET) *.o

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
