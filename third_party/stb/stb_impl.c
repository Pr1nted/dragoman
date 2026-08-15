/* The stb implementations, compiled on their own.
 *
 * They used to be generated inside Raster.cpp, which meant every warning in
 * seven thousand lines of vendored public-domain C landed in one of this
 * project's own translation units -- so `-Werror`, which the CI build wants,
 * failed on someone else's `sprintf`. Kept here instead, in a file built with
 * warnings off, while Raster.cpp includes the same headers for their
 * declarations only.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          /* the only format either game stores a layer in */
#define STBI_NO_STDIO          /* everything is read from memory already */
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
