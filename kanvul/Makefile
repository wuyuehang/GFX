VK_SDK_PATH=/home/vk/1.1.82.1/x86_64

CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include

LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs glfw3`

.PHONY: clean run

all:
	g++ $(CXXFLAGS) -o kanvul_init kanvul_init.cpp $(LDFLAGS)
	g++ $(CXXFLAGS) -o kanvul_copybuffer kanvul_copybuffer.cpp $(LDFLAGS)
	g++ $(CXXFLAGS) -o kanvul_fillbuffer kanvul_fillbuffer.cpp $(LDFLAGS)

clean:
	rm -rf kanvul_init
	rm -rf kanvul_copybuffer
	rm -rf kanvul_fillbuffer

run:
	LD_LIBRARY_PATH=$(VK_SDK_PATH)/lib VK_LAYER_PATH=$(VK_SDK_PATH)/etc/explicit_layer.d \
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump ./kanvul_init
