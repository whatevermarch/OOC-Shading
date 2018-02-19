#pragma once

#include <vulkan\vulkan.h>

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