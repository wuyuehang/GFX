VK_SDK_PATH = /home/vk/1.1.82.1/x86_64
GLFW3_PATH = /home/vk/glfw/build

#CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include
CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include -I$(GLFW3_PATH)/include

#LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs glfw3`
LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan -L$(GLFW3_PATH)/lib -lglfw
LDFLAGS_XCB = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs xcb`

.PHONY: clean run all

binary = vc_logo \
	vc_texelbuf

all: $(binary)

vc_logo : logo.cpp volcano.hpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lSOIL

vc_texelbuf : texelbuf.cpp volcano.hpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lSOIL

spv :
	$(VK_SDK_PATH)/bin/glslangValidator -V single_attribute.vert -o single_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V triple_attribute.vert -o triple_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V texelbuf.frag -o texelbuf.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V dual_attribute.frag -o dual_attribute.frag.spv

clean:
	rm -rf $(binary)
	rm -rf *.spv

run:
	LD_LIBRARY_PATH=$(VK_SDK_PATH)/lib VK_LAYER_PATH=$(VK_SDK_PATH)/etc/explicit_layer.d \
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump ./kanvul_init
