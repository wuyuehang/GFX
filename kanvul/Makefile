VK_SDK_PATH = /home/vk/1.1.82.1/x86_64
GLFW3_PATH = /home/vk/glfw/build

#CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include
CXXFLAGS = -g -std=c++11 -I$(VK_SDK_PATH)/include -I$(GLFW3_PATH)/include

#LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs glfw3`
LDFLAGS = -L$(VK_SDK_PATH)/lib -lvulkan -L$(GLFW3_PATH)/lib -lglfw
LDFLAGS_XCB = -L$(VK_SDK_PATH)/lib -lvulkan `pkg-config --libs xcb`

.PHONY: clean run all

binary = kv_init \
	kv_copybuffer \
	kv_fillbuffer \
	kv_updatebuffer \
	kv_present \
	kv_clearcolorimage \
	kv_copybuffertoimage \
	kv_compute \
	kv_draw \
	kv_vertexbuffer_00 \
	kv_indexbuffer_00 \
	kv_vertexbuffer_01 \
	kv_vertexbuffer_02 \
	kv_vertexbuffer_03 \
	kv_uniformbuffer_00 \
	kv_uniformbuffer_01 \
	kv_depthbuffer \
	kv_texture2d_00 \
	kv_xcb \
	kv_debugcallback

all: $(binary)

kv_init : kanvul_init.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_copybuffer : kanvul_copybuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_fillbuffer : kanvul_fillbuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_updatebuffer : kanvul_updatebuffer.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_present : kanvul_present.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_clearcolorimage : kanvul_clearcolorimage.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_copybuffertoimage : kanvul_copybuffertoimage.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_compute : kanvul_compute.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_draw : kanvul_draw.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_vertexbuffer_00 : kanvul_vertexbuffer_00.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_indexbuffer_00 : kanvul_indexbuffer_00.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_vertexbuffer_01 : kanvul_vertexbuffer_01.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_vertexbuffer_02 : kanvul_vertexbuffer_02.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_vertexbuffer_03 : kanvul_vertexbuffer_03.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_uniformbuffer_00 : kanvul_uniformbuffer_00.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_uniformbuffer_01 : kanvul_uniformbuffer_01.cpp resource_mgnt.hpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_depthbuffer : kanvul_depthbuffer.cpp resource_mgnt.hpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kv_texture2d_00 : kanvul_texture2d_00.cpp resource_mgnt.hpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lSOIL

kv_xcb : kanvul_xcb.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS_XCB)

kv_debugcallback : kanvul_debugcallback.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

spv :
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_compute.comp.glsl -o kanvul_compute.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_draw.vert.glsl -o kanvul_draw.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_draw.frag.glsl -o kanvul_draw.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V single_attribute.vert.glsl -o single_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V constant.frag.glsl -o constant.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V dual_attribute.vert.glsl -o dual_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V varying_color.frag.glsl -o varying_color.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V uniform.frag.glsl -o uniform.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V triple_attribute.vert.glsl -o triple_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V dual_attribute.frag.glsl -o dual_attribute.frag.spv

clean:
	rm -rf $(binary)
	rm -rf *.spv

run:
	LD_LIBRARY_PATH=$(VK_SDK_PATH)/lib VK_LAYER_PATH=$(VK_SDK_PATH)/etc/explicit_layer.d \
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump ./kanvul_init
