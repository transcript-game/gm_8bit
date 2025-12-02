#pragma once
#include <iostream>

// Static compile-time flag to toggle verbose debug logging.
// Flip to true while debugging; keep false for production builds to avoid log spam.
static constexpr bool DEBUG_MODE = true;

#define DEBUG_LOG(msg)                     \
	do {                                   \
		if (DEBUG_MODE) {                  \
			std::cout << "[gm_8bit] " << msg; \
			std::cout << std::endl;       \
		}                                  \
	} while (0)
