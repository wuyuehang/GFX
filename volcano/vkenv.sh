#!/bin/bash

export glfw3_trunk=/home/vk/glfw/build
#export loader_trunk=/home/vk/vk-trunk/Vulkan-Loader/build/install
export validation_trunk=/home/vk/vk-trunk/Vulkan-ValidationLayers/build/install
export glslang_trunk=/home/vk/vk-trunk/glslang/build/install

export LD_LIBRARY_PATH=$loader_trunk/lib:$validation_trunk/lib:$glslang_trunk/lib:$glfw3_trunk/lib

export VK_LAYER_PATH=$validation_trunk/share/vulkan/explicit_layer.d

export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation
