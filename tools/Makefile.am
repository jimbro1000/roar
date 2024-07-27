# NOTE: Run while configured for a local build if dependencies need rebuilt by
# these tools, and include those generated dependencies in the distribution.  I
# don't know how to separate the build of these tools out from any cross
# compilation configuration without including a whole extra round of running
# configure and nobody wants that.

bin_PROGRAMS = font2c

font2c_CFLAGS = $(SDL_CFLAGS)
font2c_LDADD = $(SDL_LIBS) $(SDL2_IMAGE_LIBS)
font2c_SOURCES = font2c.c
