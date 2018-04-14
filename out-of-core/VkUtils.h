#pragma once

#include <vulkan\vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <string>
#include <array>
#include <numeric>
#include <algorithm>
#include <vector>
#include <cstring>
#include <set>
#include <iomanip>


#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::ostringstream err_msg;																			\
		err_msg << "Fatal : VkResult is \"" << res << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl; \
		throw std::runtime_error(err_msg.str());															\
	}																									\
}