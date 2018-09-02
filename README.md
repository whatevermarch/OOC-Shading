# Out-of-core Shading
This project is to migrate shader resources (texture (completed), etc. (future works)) between host and device (GPU) for extended memory space by taking advantage of [Vulkan](https://www.khronos.org/vulkan/)'s memory management ability.

### WARNING!! 
Although the project is completed (only for texture migration), the source code has not been completely refactored and commented (In Progress). If you are interested in the methodology, please have a look at `ResourceManager.cpp` and start at `ResourceManager::createImage`.

### Caveats
At startup of the demo program, as investigated, the device usage seems to exceed the expected limit. That is because the program constructs textures first, and then buffers. The method `ResourceManager::createBuffer` has not been implemented completely yet, which makes the buffers non-migratable right now. But the migration works properly after changing device memory pseudolimit.

## Tools
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) (for sure)
- [AMD's Vulkan Memory Allocator](https://gpuopen.com/gaming-product/vulkan-memory-allocator/) (that default pool is like .. Oh My God)
- [GLM](https://glm.g-truc.net/)
- [GLFW](www.glfw.org/)
- [Assimp](http://assimp.sourceforge.net/)

### Special Thanks
If there ain't no [Sascha Willems's sample](https://github.com/SaschaWillems/Vulkan) and [Alexander Overvoorde's tutorial](https://vulkan-tutorial.com/), I would have got no life till now for sure. LOL
