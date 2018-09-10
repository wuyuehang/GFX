VK_SDK_PATH = /home/vk/1.1.82.1/x86_64

CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include

LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs glfw3`

.PHONY: clean run all

binary = kanvul_init \
	kanvul_copybuffer \
	kanvul_fillbuffer \
	kanvul_updatebuffer \
	kanvul_present \
	kanvul_clearcolorimage \
	kanvul_copybuffertoimage

all: $(binary)

kanvul_init : kanvul_init.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_copybuffer : kanvul_copybuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_fillbuffer : kanvul_fillbuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_updatebuffer : kanvul_updatebuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_present : kanvul_present.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_clearcolorimage : kanvul_clearcolorimage.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_copybuffertoimage : kanvul_copybuffertoimage.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(binary)

run:
	LD_LIBRARY_PATH=$(VK_SDK_PATH)/lib VK_LAYER_PATH=$(VK_SDK_PATH)/etc/explicit_layer.d \
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump ./kanvul_init
