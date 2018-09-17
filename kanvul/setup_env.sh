#!/bin/bash

export VK_SDK_PATH=/home/vk/1.1.82.1/x86_64
export LD_LIBRARY_PATH=$VK_SDK_PATH/lib
export VK_LAYER_PATH=$VK_SDK_PATH/etc/explicit_layer.d
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation
#export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation:VK_LAYER_LUNARG_api_dump
