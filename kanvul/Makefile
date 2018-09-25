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
	kanvul_copybuffertoimage \
	kanvul_compute \
	kanvul_draw \
	kanvul_gfx_template \
	kanvul_vertexbuffer_00 \
	kanvul_indexbuffer_00 \
	kanvul_vertexbuffer_01

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

kanvul_compute : kanvul_compute.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_draw : kanvul_draw.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_gfx_template : kanvul_gfx_template.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_vertexbuffer_00 : kanvul_vertexbuffer_00.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_indexbuffer_00 : kanvul_indexbuffer_00.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

kanvul_vertexbuffer_01 : kanvul_vertexbuffer_01.cpp
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

spv :
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_compute.comp.glsl -o kanvul_compute.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_draw.vert.glsl -o kanvul_draw.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V kanvul_draw.frag.glsl -o kanvul_draw.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V single_attribute.vert.glsl -o single_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V constant.frag.glsl -o constant.frag.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V dual_attribute.vert.glsl -o dual_attribute.vert.spv
	$(VK_SDK_PATH)/bin/glslangValidator -V varying_color.frag.glsl -o varying_color.frag.spv

clean:
	rm -rf $(binary)
	rm -rf *.spv

run:
	LD_LIBRARY_PATH=$(VK_SDK_PATH)/lib VK_LAYER_PATH=$(VK_SDK_PATH)/etc/explicit_layer.d \
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump ./kanvul_init
