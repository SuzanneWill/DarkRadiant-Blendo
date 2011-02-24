#ifndef RANDOMORIGIN_H_
#define RANDOMORIGIN_H_

#include "math/Vector3.h"

#include <cstdlib>
#include <string>

namespace conversation {

/**
 * Utility class containing a method to generate a random vector within a
 * certain distance of the world origin.
 *
 * TODO: Move this to a named constructor in Vector3 ?
 */
class RandomOrigin
{
public:

	/**
	 * Generate a random vector within <maxDist> of the world origin, returning
	 * a string formatted correctly as an "origin" key.
	 */
	static std::string generate(int maxDist) {

		// Generate three random numbers between 0 and maxDist
		int x = int(maxDist * (float(std::rand()) / float(RAND_MAX)));
		int y = int(maxDist * (float(std::rand()) / float(RAND_MAX)));
		int z = int(maxDist * (float(std::rand()) / float(RAND_MAX)));

		// Construct a vector and return the formatted string
		return Vector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
	}
};

} // namespace conversation

#endif /*RANDOMORIGIN_H_*/
