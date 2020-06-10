# OMAP TILER rotation shim

This shim rewrites IOCTLs to use TILER buffers to enable hardware rotation
of fullscreen OpenGL ES applications. It is intended for the Pyra but should
work on other OMAP4/5 devices, too.

Building:

    $ gcc -shared -fpic -ldl -o tiler_shim.so  -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast  tiler_shim.c

Using:
    
    $ LD_PRELOAD=tiler_shim.so LIBGL_FB=1 an_opengl_application

The LIBGL_FB=1 is for gl4es only, to force fullscreen. In other situations
you will need to enable fullscreen without X11 by other means.

Known issues:

    - Debugging needs to be tidied up/removed

This shim is licensed under a permissive ISC license.
